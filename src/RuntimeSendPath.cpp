#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail { struct RuntimeIdentity {}; }

EgressPlan::EgressPlan(EndpointDirectory::Snapshot directory, const std::vector<Route>& routes,
        const std::vector<Locator>& locals)
    // Copy construction may throw; Debug STL's noexcept vector move allocates
    // an iterator proxy and would terminate before Build can handle failure.
    : directory_(std::move(directory)), routes_(routes), locals_(locals) {}
EgressBuildResult EgressPlan::Build(const DirectoryConfiguration& config,
        const std::vector<EgressSelection>& selections)
try
{
    auto built = EndpointDirectory::Build(config);
    if (!built.value) return {nullptr, {EgressError::DirectoryRejected,
            static_cast<std::size_t>(-1), static_cast<std::size_t>(-1), built.issue}};
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const auto& selection = selections[i];
        if (!LocatorCodec::IsValid(selection.remote_locator) || !LocatorCodec::IsValid(selection.local_locator) ||
                selection.expected_route_version == 0) return {nullptr, {EgressError::InvalidSelection, i}};
        for (std::size_t j = 0; j < i; ++j) {
            if (selections[j].remote_locator == selection.remote_locator)
                return {nullptr, {EgressError::DuplicateSelection, i}};
        }
        const auto found = std::find_if(config.routes.begin(), config.routes.end(), [&](const Route& route) {
            return route.key.remote_locator == selection.remote_locator && route.key.local_locator == selection.local_locator;
        });
        if (found == config.routes.end()) return {nullptr, {EgressError::UnknownSelection, i}};
        if (found->route_version != selection.expected_route_version)
            return {nullptr, {EgressError::StaleSelection, i, static_cast<std::size_t>(found - config.routes.begin())}};
    }
    // Explicit throwing empty-vector construction also covers Debug STL proxy
    // allocations. No partial plan is published before every choice is valid.
    std::vector<Route> selected(std::size_t{0});
    std::vector<Locator> locals(std::size_t{0});
    selected.reserve(config.routes.size()); locals.reserve(config.locals.size());
    for (std::size_t i = 0; i < config.routes.size(); ++i) {
        const auto& candidate = config.routes[i];
        if (std::any_of(selected.begin(), selected.end(), [&](const Route& route) {
            return route.key.remote_locator == candidate.key.remote_locator;
        })) continue;
        const auto choice = std::find_if(selections.begin(), selections.end(), [&](const EgressSelection& selection) {
            return selection.remote_locator == candidate.key.remote_locator;
        });
        const Route* chosen = nullptr;
        for (const auto& route : config.routes) {
            if (route.key.remote_locator != candidate.key.remote_locator) continue;
            if (choice != selections.end()) {
                if (route.key.local_locator == choice->local_locator) chosen = &route;
            } else {
                if (chosen) return {nullptr, {EgressError::AmbiguousEgress, static_cast<std::size_t>(-1), i}};
                chosen = &route;
            }
        }
        selected.push_back(*chosen); // Explicit selections were proven to exist above.
        if (std::find(locals.begin(), locals.end(), chosen->key.local_locator) == locals.end())
            locals.push_back(chosen->key.local_locator);
    }
    return {Snapshot(new EgressPlan(*built.value, selected, locals)), {}};
}
catch (const std::bad_alloc&)
{
    return {nullptr, {EgressError::AllocationFailure}};
}
const Route* EgressPlan::Find(const Locator& remote) const noexcept
{
    for (const auto& route : routes_) if (route.key.remote_locator == remote) return &route;
    return nullptr;
}
std::size_t EgressPlan::size() const noexcept { return routes_.size(); }
EndpointDirectory::Snapshot EgressPlan::directory() const noexcept { return directory_; }
const std::vector<Locator>& EgressPlan::selected_locals() const noexcept { return locals_; }

RuntimeSendPath::RuntimeSendPath(EgressPlan::Snapshot plan, SendQueueLimits limits,
        std::shared_ptr<const ConnectionGenerationSource> generations)
    : RuntimeSendPath(std::move(plan), limits, std::move(generations), nullptr, {}) {}
RuntimeSendPath::RuntimeSendPath(EgressPlan::Snapshot plan, SendQueueLimits limits,
        std::shared_ptr<const ConnectionGenerationSource> generations, std::unique_ptr<SendQueue> queue, TestHooks hooks)
    : plan_(std::move(plan)), generations_(std::move(generations)),
      identity_(std::make_shared<detail::RuntimeIdentity>()), max_record_bytes_(limits.max_record_bytes),
      queue_(std::move(queue)), hooks_(std::move(hooks))
{
    if (!plan_ || !generations_ || limits.peer_records == 0 || limits.peer_bytes == 0 ||
            limits.global_records == 0 || limits.global_bytes == 0 || limits.max_record_bytes == 0 ||
            limits.max_record_bytes > (std::numeric_limits<std::uint32_t>::max)())
        throw std::invalid_argument("Runtime send path requires plan, explicit generation source and valid limits");
    if (!queue_) queue_ = std::make_unique<SendQueue>(limits);
}
RuntimeSendPath::~RuntimeSendPath() { Close(); }
RuntimeIdentityHandle RuntimeSendPath::identity() const noexcept { return identity_; }
EgressPlan::Snapshot RuntimeSendPath::plan() const noexcept { return plan_; }
bool RuntimeSendPath::stop_requested() const noexcept { return (producer_state_.load() & closed_bit_) != 0; }
bool RuntimeSendPath::TryEnterProducer() noexcept
{
    auto observed = producer_state_.load();
    for (;;) {
        if ((observed & closed_bit_) != 0 || observed == closed_bit_ - 1) return false;
        if (producer_state_.compare_exchange_weak(observed, observed + 1)) return true;
    }
}
RuntimeSendPath::Clock::time_point RuntimeSendPath::Now() const noexcept { return hooks_.now ? hooks_.now() : Clock::now(); }
RuntimeSendReport RuntimeSendPath::Submit(const std::vector<NetworkBuffer>& buffers, std::uint32_t total_bytes,
        Iterator* begin, Iterator* end, Clock::time_point original_deadline, std::int32_t priority)
{
    if (!TryEnterProducer()) return {stop_requested() ? RuntimeSendStatus::RuntimeClosed : RuntimeSendStatus::PreparationFailed};
    // This guard predates all preparation/queue temporaries. A decrement means
    // all copying, publication or refund is finished, not merely memcpy return.
    struct ProducerActivity final {
        std::atomic<std::uint64_t>& state;
        ~ProducerActivity() { state.fetch_sub(1); }
    } activity{producer_state_};
    RuntimeSendReport report;
    if (stop_requested()) return {RuntimeSendStatus::RuntimeClosed};
    if (Now() >= original_deadline) return {RuntimeSendStatus::DeadlineExpired};
    if (!begin || !end) return report;
    try {
        std::vector<SendTargetView> targets(std::size_t{0});
        targets.reserve(plan_->size());
        for (; *begin != *end; ++(*begin)) {
            if (Now() >= original_deadline) { report.status = RuntimeSendStatus::DeadlineExpired; return report; }
            const auto& locator = **begin;
            ++report.visited;
            if (locator.kind != LocatorCodec::kind) { ++report.ignored; continue; }
            if (!LocatorCodec::IsValid(locator)) { report.status = RuntimeSendStatus::InvalidDestination; return report; }
            const auto* route = plan_->Find(locator);
            if (!route) { ++report.ignored; continue; }
            if (std::any_of(targets.begin(), targets.end(), [route](const SendTargetView& target) {
                return target.route == route;
            })) { ++report.duplicates; continue; }
            targets.push_back({route, 0});
            ++report.owned_unique;
        }
        if (targets.empty()) { report.status = RuntimeSendStatus::NoOwnedTargets; return report; }
        if (total_bytes == 0 || total_bytes > max_record_bytes_) return report;
        for (auto& target : targets) {
            if (Now() >= original_deadline) { report.status = RuntimeSendStatus::DeadlineExpired; return report; }
            SendConnectionSnapshot snapshot;
            if (!generations_->TryResolve(*target.route, snapshot)) {
                if(snapshot.route_state_known)report.unresolved_route=snapshot;
                report.status = RuntimeSendStatus::UnresolvedConnection; return report;
            }
            if (snapshot.generation == 0 || snapshot.route_version == 0 ||
                    (!generations_->CurrentRouteVersions()&&snapshot.route_version!=target.route->route_version) ||
                    snapshot.key.local_locator != target.route->key.local_locator ||
                    snapshot.key.remote_locator != target.route->key.remote_locator) {
                report.status = RuntimeSendStatus::InvalidConnectionSnapshot; return report;
            }
            target.generation = snapshot.generation;
            target.route_version = snapshot.route_version;
        }
        std::vector<SendBufferView> views(std::size_t{0});
        // Only used, nonempty prefix pieces need metadata. Each accounts for
        // at least one byte, so zero-length or unused suffixes cannot grow it.
        views.reserve((std::min)(buffers.size(), static_cast<std::size_t>(total_bytes)));
        std::size_t remaining = total_bytes;
        for (const auto& buffer : buffers) {
            if (remaining == 0) break;
            if (Now() >= original_deadline) { report.status = RuntimeSendStatus::DeadlineExpired; return report; }
            const auto used = (std::min)(remaining, static_cast<std::size_t>(buffer.size));
            if (used == 0) continue;
            if (!buffer.buffer) return report;
            views.push_back({buffer.buffer, used});
            remaining -= used;
        }
        if (remaining != 0) return report;
        if (stop_requested()) { report.status = RuntimeSendStatus::RuntimeClosed; return report; }
        if (Now() >= original_deadline) { report.status = RuntimeSendStatus::DeadlineExpired; return report; }
        report.queue_result = queue_->EnqueueBatch(targets.data(), targets.size(), priority,
                original_deadline, views.data(), views.size(), total_bytes);
        report.status = *report.queue_result == SendAdmission::Accepted ? RuntimeSendStatus::Accepted : RuntimeSendStatus::QueueRejected;
        return report;
    } catch (...) {
        // Preparation/iterator/source failures cannot publish a partial batch.
        // Batch owns its own rollback and no SDK work is invoked here.
        report.status = RuntimeSendStatus::PreparationFailed;
        return report;
    }
}
std::optional<OwnedSendRecord> RuntimeSendPath::TryTake() { return queue_->TryTake(); }
SendQueueUsage RuntimeSendPath::SnapshotUsage() const { return queue_->SnapshotUsage(); }
void RuntimeSendPath::Close() noexcept
{
    {
        std::lock_guard<std::mutex> lock(attachment_mutex_);
        FreezeAdmissions();
    }
    if (hooks_.after_close_request) hooks_.after_close_request();
    queue_->Close();
}
} // namespace fastdds_dsoftbus::dsoftbus
