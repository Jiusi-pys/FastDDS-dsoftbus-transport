#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <algorithm>
#include <cstdio>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
DSoftBusTransport::DSoftBusTransport(const DSoftBusTransportDescriptor& descriptor)
    : TransportInterface(LocatorCodec::kind), frozen_(descriptor), report_(descriptor),
      mode_(FacadeMode::NativeUnavailable) {}
DSoftBusTransport::DSoftBusTransport(const DSoftBusTransportDescriptor& descriptor, HostModelTag,
        std::shared_ptr<const ConnectionGenerationSource> source)
    : TransportInterface(LocatorCodec::kind), frozen_(descriptor), report_(descriptor),
      mode_(FacadeMode::HostModel), generations_(std::move(source)) {}
DSoftBusTransport::~DSoftBusTransport() { shutdown(); }
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
DSoftBusTransport::DSoftBusTransport(const DSoftBusTransportDescriptor& descriptor, HostBackendTag,
        std::shared_ptr<HostFacadeBackend> backend)
    : TransportInterface(LocatorCodec::kind), frozen_(descriptor), report_(descriptor),
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
      mode_(FacadeMode::IsolatedExperiment), host_backend_(std::move(backend)) {}
#else
      mode_(FacadeMode::HostModel), host_backend_(std::move(backend)) {}
#endif
#endif
FacadeMode DSoftBusTransport::mode() const noexcept { return mode_; }
FacadeState DSoftBusTransport::state() const { std::lock_guard<std::mutex> lock(lifecycle_); return state_; }
FacadeDiagnostic DSoftBusTransport::last_error() const { std::lock_guard<std::mutex> lock(lifecycle_); return diagnostic_; }
void DSoftBusTransport::Diagnose(FacadeDiagnostic value) const
{ std::lock_guard<std::mutex> lock(lifecycle_); diagnostic_ = value; }
bool DSoftBusTransport::InitFailure(FacadeDiagnostic value)
{
    {
        std::lock_guard<std::mutex> lock(lifecycle_); diagnostic_ = value;
        if (state_ == FacadeState::Initializing) state_ = FacadeState::Failed;
    }
    std::fprintf(stderr, "Fast DDS DSoftBus init: error=%u operation=%s\n", static_cast<unsigned>(value.error), value.operation);
    std::fflush(stderr); return false;
}
DSoftBusTransport::Ready::Ready(EgressPlan::Snapshot selected, const DSoftBusTransportDescriptor& config,
        std::shared_ptr<const ConnectionGenerationSource> source)
    : plan(std::move(selected)), inputs(std::make_unique<InputChannels>(plan->directory(), config.maxMessageSize)),
      runtime(std::make_shared<RuntimeSendPath>(plan, config.send_limits, std::move(source))), slots(std::size_t{0})
{
    slots.reserve(config.directory.locals.size());
    for (const auto& local : config.directory.locals) slots.push_back(std::make_unique<LocalSlot>(local.local_locator));
}
DSoftBusTransport::LocalSlot* DSoftBusTransport::Ready::FindLocal(const Locator& local) const noexcept
{
    if (!LocatorCodec::IsValid(local)) return nullptr;
    for (const auto& slot : slots) if (slot->local == local) return slot.get();
    return nullptr;
}
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
DSoftBusTransport::Ready::Ready(EgressPlan::Snapshot selected, const DSoftBusTransportDescriptor& config,
        std::shared_ptr<HostFacadeBackend> backend)
    : host_backend(std::move(backend)), plan(std::move(selected))
{
    for (const auto& local : config.directory.locals)
        slots.push_back(std::make_unique<LocalSlot>(local.local_locator));
}
DSoftBusTransport::Ready::~Ready() { if (host_backend && host_shutdown_owned) host_backend->Shutdown(); }
#endif
bool DSoftBusTransport::init(const fastdds_dsoftbus::binding::PropertyPolicy*, const std::uint32_t& no_frag)
{
    {
        std::lock_guard<std::mutex> lock(lifecycle_);
        if (state_ != FacadeState::Constructed) {
            // Keep a failed initialization's original diagnostic available.
            if (state_ != FacadeState::Failed) diagnostic_ = {FacadeError::InvalidState, "init"};
            return false;
        }
        state_ = FacadeState::Initializing;
    }
    try {
        const auto& limits = frozen_.send_limits;
        if (!frozen_.maxMessageSize || frozen_.maxInitialPeersRange != 1 ||
                limits.max_record_bytes != frozen_.maxMessageSize || !limits.peer_records || !limits.peer_bytes ||
                !limits.global_records || !limits.global_bytes || limits.max_record_bytes > limits.peer_bytes ||
                limits.max_record_bytes > limits.global_bytes)
            return InitFailure({FacadeError::InvalidLimits, "limits"});
        if (no_frag > frozen_.maxMessageSize)
            return InitFailure({FacadeError::UnsupportedNoFragmentRequest, "no_fragment_limit"});
        const auto built = EgressPlan::Build(frozen_.directory, frozen_.egress_selections);
        if (!built.value) {
            auto error = built.issue.code == EgressError::AllocationFailure ? FacadeError::AllocationFailure :
                    built.issue.code == EgressError::DirectoryRejected ? FacadeError::DirectoryRejected : FacadeError::EgressRejected;
            return InitFailure({error, "egress", built.issue.route_index, built.issue.directory_issue, built.issue});
        }
        for (const auto* defaults : {&frozen_.default_metatraffic_unicast, &frozen_.default_user_unicast}) {
            if (defaults->empty()) return InitFailure({FacadeError::InvalidDefaultLocal, "defaults"});
            for (std::size_t i = 0; i < defaults->size(); ++i) {
                const auto& local = (*defaults)[i];
                const bool known = LocatorCodec::IsValid(local) && std::any_of(frozen_.directory.locals.begin(),
                        frozen_.directory.locals.end(), [&](const LocalBinding& binding) { return binding.local_locator == local; });
                if (!known || std::find(defaults->begin(), defaults->begin() + i, local) != defaults->begin() + i)
                    return InitFailure({FacadeError::InvalidDefaultLocal, "defaults", i});
            }
        }
        if (mode_ == FacadeMode::NativeUnavailable)
            return InitFailure({FacadeError::NativeBackendUnavailable, "NativeBackendUnavailable"});
        std::shared_ptr<Ready> prepared;
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
        if (host_backend_) {
            prepared = std::make_shared<Ready>(built.value, frozen_, host_backend_);
            if (!host_backend_->Start(frozen_))
                return InitFailure({FacadeError::NativeBackendUnavailable, "host_backend_start"});
            prepared->host_shutdown_owned = true;
        } else
#endif
        {
            if (!generations_) return InitFailure({FacadeError::InvalidState, "host_model_source"});
            prepared = std::make_shared<Ready>(built.value, frozen_, generations_);
        }
        if (hooks_.before_ready_publish) hooks_.before_ready_publish();
        {
            std::lock_guard<std::mutex> lock(lifecycle_);
            if (state_ != FacadeState::Initializing) {
                diagnostic_ = {FacadeError::InvalidState, "init_publication_after_close"};
                return false; // Destroy unpublished cores outside the lifecycle lock.
            }
            ready_ = std::move(prepared); state_ = FacadeState::ModelReady; diagnostic_ = {};
        }
        return true;
    } catch (const std::bad_alloc&) { return InitFailure({FacadeError::AllocationFailure, "init_allocation"}); }
}
std::shared_ptr<DSoftBusTransport::Ready> DSoftBusTransport::ReadyLease() const
{ std::lock_guard<std::mutex> lock(lifecycle_); return state_ == FacadeState::ModelReady ? ready_ : nullptr; }
bool DSoftBusTransport::IsInputChannelOpen(const Locator& local) const
{ auto ready = ReadyLease(); auto* slot = ready ? ready->FindLocal(local) : nullptr; return slot && slot->committed_open.load(); }
bool DSoftBusTransport::IsLocatorSupported(const Locator& l) const { return LocatorCodec::IsValid(l); }
bool DSoftBusTransport::is_locator_allowed(const Locator& locator) const
{ auto r = ReadyLease(); return r && LocatorCodec::IsValid(locator) && (r->FindLocal(locator) || r->plan->Find(locator)); }
bool DSoftBusTransport::is_locator_reachable(const Locator& locator)
{ auto r = ReadyLease(); return r && LocatorCodec::IsValid(locator) && r->plan->Find(locator); }
Locator DSoftBusTransport::RemoteToMainLocal(const Locator& remote) const
{ auto r = ReadyLease(); const auto* route = r && LocatorCodec::IsValid(remote) ? r->plan->Find(remote) : nullptr; return route ? route->key.local_locator : Locator{}; }
bool DSoftBusTransport::transform_remote_locator(const Locator& remote, Locator& out) const
{ auto r = ReadyLease(); if (!r || !LocatorCodec::IsValid(remote) || !r->plan->Find(remote)) return false; out = remote; return true; }
bool DSoftBusTransport::OpenOutputChannel(SendResourceList& list, const Locator& hint)
{
    auto r = ReadyLease();
    if (!r) { Diagnose({FacadeError::InvalidState, "open_output"}); return false; }
    if (!LocatorCodec::IsValid(hint) || (!r->FindLocal(hint) && !r->plan->Find(hint))) {
        Diagnose({FacadeError::InvalidLocator, "open_output"}); return false;
    }
    const auto attached =
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
        r->host_backend ? r->host_backend->Attach(list) :
#endif
        DSoftBusSenderResource::Attach(list, r->runtime);
    if (!attached.ok()) Diagnose({FacadeError::SenderAttachRejected, "open_output"});
    return attached.ok();
}
bool DSoftBusTransport::OpenOutputChannels(SendResourceList& list, const SelectorEntry& entry)
{
    auto r = ReadyLease(); if (!r) return false;
    bool owned = false;
    for (auto index : entry.state.multicast) {
        if (index >= entry.multicast.size() || entry.multicast[index].kind == LocatorCodec::kind) return false;
    }
    for (auto index : entry.state.unicast) {
        if (index >= entry.unicast.size()) return false;
        const auto& local = entry.unicast[index];
        if (local.kind != LocatorCodec::kind) continue;
        if (!LocatorCodec::IsValid(local)) return false;
        if (r->plan->Find(local)) owned = true;
    }
    if (!owned) return false;
    const auto result =
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
        r->host_backend ? r->host_backend->Attach(list) :
#endif
        DSoftBusSenderResource::Attach(list, r->runtime);
    if (!result.ok()) Diagnose({FacadeError::SenderAttachRejected, "open_outputs"});
    return result.ok();
}
void DSoftBusTransport::CloseOutputChannels(SendResourceList&, const SelectorEntry&)
{ /* Aggregate resource still serves other endpoints; only list-owner Release or shutdown stops it. */ }
bool DSoftBusTransport::OpenInputChannel(const Locator& local, Receiver* receiver, std::uint32_t capacity)
{
    auto r = ReadyLease(); if (!r) { Diagnose({FacadeError::InvalidState, "open_input"}); return false; }
    auto* slot = r->FindLocal(local);
    if (!slot || !receiver) { Diagnose({FacadeError::InvalidLocator, "open_input"}); return false; }
    if (capacity < frozen_.maxMessageSize) { Diagnose({FacadeError::InsufficientReceiverCapacity, "open_input"}); return false; }
    std::lock_guard<std::mutex> control(slot->control);
    if (!ReadyLease()) { Diagnose({FacadeError::InvalidState, "open_input"}); return false; }
    const auto result =
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
        r->host_backend ? r->host_backend->Open(local, receiver) :
#endif
        r->inputs->Open(local, receiver);
    if (result != InputOpenResult::Opened && result != InputOpenResult::AlreadyOpen) {
        Diagnose({result == InputOpenResult::AllocationFailure ? FacadeError::AllocationFailure :
            result == InputOpenResult::Closing || result == InputOpenResult::Disabled ? FacadeError::InputClosing : FacadeError::InputConflict,
            "open_input"}); return false;
    }
    if (hooks_.after_core_open) hooks_.after_core_open();
    {
        std::lock_guard<std::mutex> lock(lifecycle_);
        if (state_ == FacadeState::ModelReady) { slot->committed_open.store(true); return true; }
    }
    slot->committed_open.store(false);
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
    if (r->host_backend) r->host_backend->Shutdown(); else
#endif
    r->inputs->Close(local); // Roll back before reporting false: caller may destroy receiver.
    Diagnose({FacadeError::InputClosing, "open_input_rollback"}); return false;
}
bool DSoftBusTransport::CloseInputChannel(const Locator& local)
{
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
    // Refuse before any outer lock: an external close may be draining this callback.
    if (host_backend_ && !host_backend_->InputCloseAllowedOnThisThread()) return false;
#endif
    std::shared_ptr<Ready> r;
    { std::lock_guard<std::mutex> lock(lifecycle_); r = ready_; }
    auto* slot = r ? r->FindLocal(local) : nullptr;
    if (!slot) { Diagnose({FacadeError::InvalidLocator, "close_input"}); return false; }
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
    if (r->host_backend) {
        std::lock_guard<std::mutex> control(slot->control);
        const bool was_open=slot->committed_open.exchange(false);
        const bool closed=r->host_backend->CloseInput(local);
        if(!closed) {slot->committed_open.store(was_open);Diagnose({FacadeError::InputClosing,"host_input_close_failed"});}
        return closed;
    }
#endif
    std::lock_guard<std::mutex> control(slot->control);
    slot->committed_open.store(false);
    if (hooks_.after_close_mark) hooks_.after_close_mark();
    r->inputs->Close(local); return true;
}
bool DSoftBusTransport::DoInputLocatorsMatch(const Locator& a, const Locator& b) const
{ auto r = ReadyLease(); return r && a == b && r->FindLocal(a); }
DSoftBusTransport::LocatorList DSoftBusTransport::NormalizeLocator(const Locator& locator)
{ LocatorList result; if (is_locator_allowed(locator)) result.push_back(locator); return result; }
void DSoftBusTransport::select_locators(Selector& selector) const
{
    auto& entries = selector.transport_starts(); auto r = ReadyLease(); if (!r) return;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto* entry = entries[i]; if (!entry->transport_should_process) continue;
#if !defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        if (!entry->allowed_to_send) continue;
#else
        // The actual 2.14 selector seeds transport_should_process from enabled.
        // It has no later allowed_to_send field; admission still uses the same
        // frozen egress plan and per-target core gates below and in Send.
        if (!entry->enabled) continue;
#endif
        bool added = false;
        for (std::size_t index = 0; index < entry->unicast.size(); ++index) {
            const auto& remote = entry->unicast[index];
            if (!LocatorCodec::IsValid(remote) || !r->plan->Find(remote) || selector.is_selected(remote)) continue;
            const bool duplicate = std::any_of(entry->state.unicast.begin(), entry->state.unicast.end(), [&](std::size_t prior) {
                return prior < entry->unicast.size() && entry->unicast[prior] == remote;
            });
            if (!duplicate && entry->state.unicast.push_back(index)) added = true;
        }
        if (added) selector.select(i);
    }
}
bool DSoftBusTransport::is_local_locator(const Locator& locator) const
{ auto r = ReadyLease(); return r && r->FindLocal(locator); }
eprosima::fastdds::rtps::TransportDescriptorInterface* DSoftBusTransport::get_configuration() { return &report_; }
bool DSoftBusTransport::AppendDefaults(LocatorList& out, const std::vector<Locator>& values) const
{
    if (!ReadyLease() || values.empty()) return false;
    LocatorList prepared(out);
    for (const auto& value : values) prepared.push_back(value);
    out.swap(prepared); return true;
}
void DSoftBusTransport::AddDefaultOutputLocator(LocatorList& out)
{ auto r = ReadyLease(); if (r) AppendDefaults(out, r->plan->selected_locals()); }
bool DSoftBusTransport::getDefaultMetatrafficMulticastLocators(LocatorList&, std::uint32_t) const { return false; }
bool DSoftBusTransport::getDefaultMetatrafficUnicastLocators(LocatorList& out, std::uint32_t) const
{ return AppendDefaults(out, frozen_.default_metatraffic_unicast); }
bool DSoftBusTransport::getDefaultUnicastLocators(LocatorList& out, std::uint32_t) const
{ return AppendDefaults(out, frozen_.default_user_unicast); }
bool DSoftBusTransport::fillMetatrafficMulticastLocator(Locator&, std::uint32_t) const { return false; }
bool DSoftBusTransport::fillMetatrafficUnicastLocator(Locator& local, std::uint32_t) const { return is_local_locator(local); }
bool DSoftBusTransport::configureInitialPeerLocator(Locator& remote, const fastdds_dsoftbus::binding::PortParameters&,
        std::uint32_t, LocatorList& out) const
{
    auto r = ReadyLease(); if (!r || !LocatorCodec::IsValid(remote) || !r->plan->Find(remote)) return false;
    LocatorList prepared(out); prepared.push_back(remote); out.swap(prepared); return true;
}
bool DSoftBusTransport::fillUnicastLocator(Locator& local, std::uint32_t) const { return is_local_locator(local); }
std::uint32_t DSoftBusTransport::max_recv_buffer_size() const { return ReadyLease() ? frozen_.maxMessageSize : 0; }
bool DSoftBusTransport::is_localhost_allowed() const { return false; }
void DSoftBusTransport::update_network_interfaces() {}
void DSoftBusTransport::shutdown()
{
    std::shared_ptr<Ready> r;
    { std::lock_guard<std::mutex> lock(lifecycle_); state_ = FacadeState::Closing; r = ready_; }
    if (hooks_.after_shutdown_request) hooks_.after_shutdown_request();
    if (r) {
#if defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT)
        if (r->host_backend) {
            if (!r->host_backend->Shutdown()) Diagnose({FacadeError::InvalidState, "host_shutdown_failed"});
            for (const auto& slot : r->slots) slot->committed_open.store(false);
        } else
#endif
        {
        r->runtime->Close();
        for (const auto& slot : r->slots) slot->committed_open.store(false);
        r->inputs->DisableAll();
        }
    }
    { std::lock_guard<std::mutex> lock(lifecycle_); state_ = FacadeState::Closed; }
}
} // namespace fastdds_dsoftbus::dsoftbus
