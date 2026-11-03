#include <fastdds_dsoftbus/dsoftbus/DSoftBusSenderResource.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

thread_local bool send_allocation_armed = false;
thread_local std::size_t send_allocation_index = 0;
thread_local std::size_t send_fail_at = (std::numeric_limits<std::size_t>::max)();
thread_local std::size_t send_allocation_failures = 0;
void* operator new(std::size_t size)
{
    if (send_allocation_armed && send_allocation_index++ == send_fail_at) {
        ++send_allocation_failures;
        throw std::bad_alloc();
    }
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace fastdds_dsoftbus::dsoftbus;
using namespace std::chrono_literals;
namespace rtps = eprosima::fastdds::rtps;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace {
class SendAllocationScope
{
public:
    explicit SendAllocationScope(std::size_t at = (std::numeric_limits<std::size_t>::max)())
    { send_allocation_index = 0; send_allocation_failures = 0; send_fail_at = at; send_allocation_armed = true; }
    ~SendAllocationScope() { send_allocation_armed = false; }
};
class SendTestEvent
{
public:
    void Signal() { std::lock_guard<std::mutex> lock(mutex_); ++count_; changed_.notify_all(); }
    void Wait(std::size_t count = 1)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!changed_.wait_for(lock, 3s, [&] { return count_ >= count; })) {
            std::cerr << "FAIL synchronization watchdog" << std::endl; std::abort();
        }
    }
private:
    std::mutex mutex_; std::condition_variable changed_; std::size_t count_ = 0;
};
template<class T> T Finish(std::future<T>& future)
{
    if (future.wait_for(3s) != std::future_status::ready) {
        std::cerr << "FAIL completion watchdog" << std::endl; std::abort();
    }
    return future.get();
}
Locator SendTestLocator(std::uint8_t id)
{
    EndpointId endpoint{}; endpoint[15] = id; Locator result;
    CHECK(LocatorCodec::Encode(endpoint, result)); return result;
}
Route SendTestRoute(std::uint8_t local, std::uint8_t remote, std::uint64_t version = 1)
{
    Route route; route.key = {SendTestLocator(local), SendTestLocator(remote)}; route.route_version = version;
    const auto l = std::to_string(local), r = std::to_string(remote);
    route.fields = {"network-" + r, "listen-" + r, "dial-" + l + "-" + r,
            {"peer-" + r + "-" + l}, RecordDataType::Bytes};
    return route;
}
DirectoryConfiguration SendTestConfig(bool ambiguous = false)
{
    DirectoryConfiguration config;
    config.locals = {{SendTestLocator(2), "namespace-a", "listen-a"},
            {SendTestLocator(3), "namespace-b", "listen-b"}};
    config.routes = {SendTestRoute(2, 10, 5), SendTestRoute(3, 11, 6)};
    if (ambiguous) config.routes.push_back(SendTestRoute(3, 10, 7));
    return config;
}
class ControlledGenerationSource final : public ConnectionGenerationSource
{
public:
    using Strategy = std::function<bool(const Route&, SendConnectionSnapshot&)>;
    explicit ControlledGenerationSource(Strategy strategy = {}) : strategy_(std::move(strategy)) {}
    bool TryResolve(const Route& route, SendConnectionSnapshot& output) const override
    {
        ++calls;
        if (strategy_) return strategy_(route, output);
        output = {route.key, route.route_version, 100 * route.key.remote_locator.address[15] + route.route_version};
        return true;
    }
    mutable std::atomic<std::size_t> calls{0};
private:
    const Strategy strategy_;
};
SendQueueLimits SendTestLimits() { return {8, 1024, 16, 4096, 1024}; }
EgressPlan::Snapshot SendTestPlan(const DirectoryConfiguration& config = SendTestConfig())
{
    auto built = EgressPlan::Build(config, {}); CHECK(built.value); return built.value;
}
std::shared_ptr<RuntimeSendPath> SendTestRuntime(EgressPlan::Snapshot plan = SendTestPlan(),
        std::shared_ptr<const ConnectionGenerationSource> source = std::make_shared<ControlledGenerationSource>(),
        SendQueueLimits limits = SendTestLimits())
{ return std::make_shared<RuntimeSendPath>(std::move(plan), limits, std::move(source)); }
DSoftBusSenderResource& AttachSendTest(rtps::SendResourceList& list, const std::shared_ptr<RuntimeSendPath>& runtime)
{
    CHECK(DSoftBusSenderResource::Attach(list, runtime).ok());
    for (auto& entry : list) {
        auto* sender = dynamic_cast<DSoftBusSenderResource*>(entry.get());
        if (sender && sender->belongs_to(runtime->identity())) return *sender;
    }
    throw std::runtime_error("attached sender absent");
}
bool SendTest(rtps::SenderResource& sender, const std::vector<Locator>& destinations,
        const std::vector<rtps::NetworkBuffer>& buffers = {}, std::uint32_t bytes = 1,
        std::int32_t priority = 0, SendQueue::Clock::time_point deadline = SendQueue::Clock::now() + 1s,
        bool legacy = false)
{
    const std::uint8_t byte = 42;
    const std::vector<rtps::NetworkBuffer> fallback{{&byte, 1}};
    rtps::Locators begin(destinations.cbegin()), end(destinations.cend());
    if (legacy) return sender.send(buffers.empty() ? fallback : buffers, bytes, &begin, &end, deadline);
    return sender.send(buffers.empty() ? fallback : buffers, bytes, &begin, &end, deadline, priority);
}
// Direct dependency regressions: Directory is allowed to propagate bad_alloc,
// but not to terminate or mutate an already-published snapshot. Inputs and the
// old snapshot are fully constructed before injection is armed.
std::size_t DirectoryAllocationProbe(const std::string& mode, std::size_t fail_at)
{
    const auto config = SendTestConfig(true);
    const auto initial = EndpointDirectory::Build(config); CHECK(initial.value);
    const auto old = *initial.value;
    const auto key = config.routes.front().key;
    auto replacement = config.routes.front().fields;
    replacement.remote_network_id = "replacement-net";
    const auto operation = [&] {
        if (mode == "build") {
            const auto result = EndpointDirectory::Build(config);
            return result.value.has_value();
        }
        if (mode == "routes") {
            const auto result = old->RoutesTo(key.remote_locator);
            return result.value && result.value->size() == 2 && result.value->front().route_version == 5;
        }
        const auto result = old->WithUpdatedRoute(key, 5, replacement);
        return result.value.has_value();
    };
    bool success = false, caught = false;
    std::size_t attempted, failures;
    {
        SendAllocationScope fail(fail_at);
        try { success = operation(); }
        catch (const std::bad_alloc&) { caught = true; }
        attempted = send_allocation_index; failures = send_allocation_failures;
    }
    if (fail_at == (std::numeric_limits<std::size_t>::max)()) {
        CHECK(success && !caught && failures == 0 && attempted != 0);
    } else {
        CHECK(!success && caught && failures == 1 && attempted == fail_at + 1);
    }
    const auto unchanged = old->ResolveOutbound(key);
    CHECK(unchanged.value && unchanged.value->route_version == 5);
    CHECK(unchanged.value->fields.remote_network_id == "network-10");
    CHECK(config.routes.front().route_version == 5 && config.routes.front().fields.remote_network_id == "network-10");
    CHECK(operation()); // After every injected failure the next call succeeds.
    if (mode == "update") {
        const auto updated = old->WithUpdatedRoute(key, 5, replacement); CHECK(updated.value);
        const auto route = (*updated.value)->ResolveOutbound(key);
        CHECK(route.value && route.value->route_version == 6 && route.value->fields.remote_network_id == "replacement-net");
    }
    return attempted;
}
}

namespace fastdds_dsoftbus::dsoftbus {
class SendQueueTestAccess
{
public:
    static std::unique_ptr<SendQueue> Make(SendQueueLimits limits,
            std::function<SendQueue::Clock::time_point()> now = {}, std::function<void()> before = {},
            std::function<void()> after = {}, std::function<void()> wait = {})
    { return std::unique_ptr<SendQueue>(new SendQueue(limits, {std::move(now), std::move(wait), std::move(before), std::move(after)})); }
};
class RuntimeSendPathTestAccess
{
public:
    static void Freeze(RuntimeSendPath& runtime) { runtime.FreezeAdmissions(); }
    static std::uint64_t Producers(const RuntimeSendPath& runtime) { return runtime.ProducerCount(); }
    static void Saturate(RuntimeSendPath& runtime) { runtime.producer_state_.store(runtime.closed_bit_ - 1); }
    static void ResetSaturation(RuntimeSendPath& runtime) { runtime.producer_state_.store(0); }
    static std::shared_ptr<RuntimeSendPath> Make(EgressPlan::Snapshot plan, SendQueueLimits limits,
            std::shared_ptr<const ConnectionGenerationSource> source, std::unique_ptr<SendQueue> queue,
            std::function<SendQueue::Clock::time_point()> now = {}, std::function<void()> attach = {},
            std::function<void()> close = {})
    {
        return std::shared_ptr<RuntimeSendPath>(new RuntimeSendPath(std::move(plan), limits, std::move(source),
                std::move(queue), {std::move(now), std::move(attach), std::move(close)}));
    }
    static void OnRelease(DSoftBusSenderResource& sender, std::function<void()> observer)
    { sender.on_release_ = std::move(observer); } // Before concurrent access only.
};
}

int RunRuntimeSendPathTests(int argc, char** argv)
{
    if (argc == 4 && std::string(argv[1]) == "--directory-allocation") {
        const std::string mode = argv[2];
        CHECK(mode == "build" || mode == "routes" || mode == "update");
        const auto at = static_cast<std::size_t>(std::stoull(argv[3]));
        const auto count = DirectoryAllocationProbe(mode, at);
        std::cout << "directory " << mode << " allocation index=" << at << " attempted=" << count << " PASS" << std::endl;
        return 0;
    }
    if (argc == 3 && (std::string(argv[1]) == "--plan-allocation" || std::string(argv[1]) == "--plan-empty-allocation")) {
        const auto at = static_cast<std::size_t>(std::stoull(argv[2]));
        auto config = SendTestConfig(); const std::vector<EgressSelection> choices;
        if (std::string(argv[1]) == "--plan-empty-allocation") { config.routes.clear(); config.locals.clear(); }
        EgressBuildResult result;
        { SendAllocationScope fail(at); result = EgressPlan::Build(config, choices); }
        std::cout << "plan allocation index=" << at << " attempted=" << send_allocation_index
                  << " failures=" << send_allocation_failures << " status=" << static_cast<int>(result.issue.code) << std::endl;
        return !result.value && result.issue.code == EgressError::AllocationFailure && send_allocation_failures == 1 ? 0 : 1;
    }
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    cases.emplace_back("DSP-N3-producer-freeze-and-refund", [] {
        SendTestEvent copying, resume;
        auto source=std::make_shared<ControlledGenerationSource>();
        auto limits=SendTestLimits();
        auto runtime=RuntimeSendPathTestAccess::Make(SendTestPlan(),limits,source,
            SendQueueTestAccess::Make(limits,{},[&]{copying.Signal();resume.Wait();}));
        rtps::SendResourceList first,second;
        auto& sender=AttachSendTest(first,runtime);auto& later=AttachSendTest(second,runtime);
        const std::vector<Locator> targets{SendTestLocator(10)};
        auto sending=std::async(std::launch::async,[&]{return SendTest(sender,targets);});copying.Wait();
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==1);
        RuntimeSendPathTestAccess::Freeze(*runtime); // No queue Close or provider wait here.
        const auto calls=source->calls.load();
        CHECK(!SendTest(later,targets));
        CHECK(later.last_report()->status==RuntimeSendStatus::RuntimeClosed);
        CHECK(source->calls==calls);
        runtime->Close();
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==1);
        CHECK(runtime->SnapshotUsage().records==1); // Original copy still owns its charge.
        resume.Signal();CHECK(!Finish(sending));
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==0);
        CHECK(runtime->SnapshotUsage().records==0&&runtime->SnapshotUsage().bytes==0);
    });
    cases.emplace_back("DSP-N3-retained-record-not-producer", [] {
        auto runtime=SendTestRuntime();rtps::SendResourceList list;auto& sender=AttachSendTest(list,runtime);
        CHECK(SendTest(sender,{SendTestLocator(10)}));
        auto retained=runtime->TryTake();CHECK(retained);
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==0);
        RuntimeSendPathTestAccess::Freeze(*runtime);runtime->Close();
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==0&&runtime->SnapshotUsage().records==1);
        retained.reset();CHECK(runtime->SnapshotUsage().records==0);
    });
    cases.emplace_back("DSP-N3-producer-overflow-rejected", [] {
        auto source=std::make_shared<ControlledGenerationSource>();auto runtime=SendTestRuntime(SendTestPlan(),source);
        rtps::SendResourceList list;auto& sender=AttachSendTest(list,runtime);
        RuntimeSendPathTestAccess::Saturate(*runtime); // Test-only, no actual calls outstanding.
        const bool accepted=SendTest(sender,{SendTestLocator(10)});
        const auto report=sender.last_report();const auto count=RuntimeSendPathTestAccess::Producers(*runtime);
        RuntimeSendPathTestAccess::ResetSaturation(*runtime);
        CHECK(!accepted&&report->status==RuntimeSendStatus::PreparationFailed);
        CHECK(count==(std::uint64_t{1}<<63)-1&&source->calls==0);
        CHECK(SendTest(sender,{SendTestLocator(10)}));
        CHECK(RuntimeSendPathTestAccess::Producers(*runtime)==0);
    });
    cases.emplace_back("DSP-01-egress-auto-and-owned-snapshot", [] {
        auto config = SendTestConfig(); auto built = EgressPlan::Build(config, {});
        CHECK(built.value && built.issue.code == EgressError::None && built.value->size() == 2);
        config.routes.clear(); config.locals.clear();
        const auto* route = built.value->Find(SendTestLocator(10));
        CHECK(route && route->key.local_locator == SendTestLocator(2) && route->route_version == 5);
        CHECK(route->fields.remote_network_id == "network-10");
        CHECK(built.value->directory()->ResolveOutbound(route->key).value.has_value());
        CHECK(built.value->selected_locals().size() == 2 && !built.value->Find(SendTestLocator(12)));
    });
    cases.emplace_back("DSP-02-egress-explicit-and-ambiguous", [] {
        auto config = SendTestConfig(true);
        CHECK(EgressPlan::Build(config, {}).issue.code == EgressError::AmbiguousEgress);
        const EgressSelection choose{SendTestLocator(10), SendTestLocator(3), 7};
        auto built = EgressPlan::Build(config, {choose});
        CHECK(built.value && built.value->Find(SendTestLocator(10))->key.local_locator == SendTestLocator(3));
        CHECK(built.value->selected_locals().size() == 1);
    });
    cases.emplace_back("DSP-03-egress-selection-errors", [] {
        auto config = SendTestConfig(); EgressSelection choice{SendTestLocator(10), SendTestLocator(2), 5};
        CHECK(EgressPlan::Build(config, {choice, choice}).issue.code == EgressError::DuplicateSelection);
        choice.expected_route_version = 0;
        CHECK(EgressPlan::Build(config, {choice}).issue.code == EgressError::InvalidSelection);
        choice.expected_route_version = 99;
        CHECK(EgressPlan::Build(config, {choice}).issue.code == EgressError::StaleSelection);
        choice.expected_route_version = 5; choice.local_locator = SendTestLocator(3);
        CHECK(EgressPlan::Build(config, {choice}).issue.code == EgressError::UnknownSelection);
        choice.remote_locator = SendTestLocator(12);
        CHECK(EgressPlan::Build(config, {choice}).issue.code == EgressError::UnknownSelection);
        choice.remote_locator.port = 0;
        CHECK(EgressPlan::Build(config, {choice}).issue.code == EgressError::InvalidSelection);
    });
    cases.emplace_back("DSP-04-egress-directory-rejected-and-empty", [] {
        auto config = SendTestConfig(); config.routes[1].route_version = 0;
        const auto rejected = EgressPlan::Build(config, {});
        CHECK(!rejected.value && rejected.issue.code == EgressError::DirectoryRejected);
        CHECK(rejected.issue.directory_issue.code == DirectoryError::InvalidVersion);
        const auto empty = EgressPlan::Build({}, {});
        CHECK(empty.value && empty.value->size() == 0 && empty.value->selected_locals().empty());
    });
    cases.emplace_back("DSP-05-empty-egress-real-allocation-sweep", [] {
        const DirectoryConfiguration config; const std::vector<EgressSelection> choices;
        std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            EgressBuildResult result; std::size_t attempted, failures;
            {
                SendAllocationScope fail(iteration == 0 ? (std::numeric_limits<std::size_t>::max)() : iteration - 1);
                result = EgressPlan::Build(config, choices);
                attempted = send_allocation_index; failures = send_allocation_failures;
            }
            if (iteration == 0) { CHECK(result.value && result.value->size() == 0 && failures == 0); count = attempted; }
            else {
                CHECK(!result.value && result.issue.code == EgressError::AllocationFailure && failures == 1 && attempted == iteration);
                CHECK(EgressPlan::Build(config, choices).value);
            }
            if (iteration == count) break;
        }
        std::cout << "empty egress allocation failure points=" << count << std::endl;
    });
    cases.emplace_back("DSP-06-real-base-priority-legacy-and-owned-batch", [] {
        auto runtime = SendTestRuntime(); rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        const auto deadline = SendQueue::Clock::now() + 1s;
        for (int mode = 0; mode < 3; ++mode) {
            const auto priority = mode == 0 ? 37 : -9;
            {
                std::vector<Locator> targets{SendTestLocator(11), SendTestLocator(10)};
                std::vector<std::uint8_t> bytes{1, 2, 3, 4};
                CHECK(SendTest(sender, targets, {{bytes.data(), 2}, {nullptr, 0}, {bytes.data() + 2, 2}, {nullptr, UINT32_MAX}},
                        3, priority, deadline, mode == 2));
                std::fill(bytes.begin(), bytes.end(), std::uint8_t{99}); targets.clear();
            }
            auto first = runtime->TryTake(), second = runtime->TryTake();
            CHECK(first && second && !runtime->TryTake());
            CHECK(first->route().key.remote_locator == SendTestLocator(11) && first->route().key.local_locator == SendTestLocator(3));
            CHECK(second->route().key.remote_locator == SendTestLocator(10) && second->route().key.local_locator == SendTestLocator(2));
            CHECK(first->generation() == 1106 && second->generation() == 1005);
            CHECK(first->priority() == (mode == 2 ? 0 : priority) && second->priority() == first->priority());
            CHECK(first->deadline() == deadline && first->data()[0] == 1 && first->data()[2] == 3 && first->size() == 3);
            CHECK(second->data() != first->data() && second->data()[2] == 3);
        }
    });
    cases.emplace_back("DSP-07-mixed-unowned-duplicates-and-no-owned", [] {
        auto source = std::make_shared<ControlledGenerationSource>(); auto runtime = SendTestRuntime(SendTestPlan(), source);
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        auto udp = SendTestLocator(10); udp.kind = 1;
        CHECK(SendTest(sender, {udp, SendTestLocator(12), SendTestLocator(11), SendTestLocator(11), SendTestLocator(10)}));
        auto report = sender.last_report();
        CHECK(report && report->visited == 5 && report->owned_unique == 2 && report->duplicates == 1 && report->ignored == 2);
        CHECK(report->queue_result == SendAdmission::Accepted && source->calls == 2);
        auto first = runtime->TryTake(), second = runtime->TryTake(); CHECK(first && second && !runtime->TryTake());
        CHECK(first->route().key.remote_locator == SendTestLocator(11));
        CHECK(!SendTest(sender, {udp, SendTestLocator(12)}));
        CHECK(sender.last_report()->status == RuntimeSendStatus::NoOwnedTargets && source->calls == 2);
        auto malformed = SendTestLocator(10); malformed.port = 0;
        CHECK(!SendTest(sender, {SendTestLocator(10), malformed}));
        CHECK(sender.last_report()->status == RuntimeSendStatus::InvalidDestination && !runtime->TryTake());
    });
    cases.emplace_back("DSP-08-generation-failure-publishes-no-prefix", [] {
        for (int failure = 0; failure < 5; ++failure) {
            auto source = std::make_shared<ControlledGenerationSource>([failure](const Route& route, SendConnectionSnapshot& output) {
                output = {route.key, route.route_version, 9};
                if (route.key.remote_locator != SendTestLocator(11)) return true;
                if (failure == 0) return false;
                if (failure == 1) output.generation = 0;
                if (failure == 2) output.key.local_locator = SendTestLocator(2);
                if (failure == 3) ++output.route_version;
                if (failure == 4) throw std::runtime_error("explicit source fault");
                return true;
            });
            auto runtime = SendTestRuntime(SendTestPlan(), source); rtps::SendResourceList list;
            auto& sender = AttachSendTest(list, runtime);
            CHECK(!SendTest(sender, {SendTestLocator(10), SendTestLocator(11)}));
            const auto expected = failure == 0 ? RuntimeSendStatus::UnresolvedConnection :
                    failure == 4 ? RuntimeSendStatus::PreparationFailed : RuntimeSendStatus::InvalidConnectionSnapshot;
            CHECK(sender.last_report()->status == expected && !sender.last_report()->queue_result);
            CHECK(!runtime->TryTake() && runtime->SnapshotUsage().records == 0);
        }
    });
    cases.emplace_back("DSP-09-runtime-input-and-construction-errors", [] {
        auto plan = SendTestPlan();
        for (int mode = 0; mode < 4; ++mode) {
            bool rejected = false; auto limits = SendTestLimits();
            if (mode == 2) limits.peer_records = 0;
            if (mode == 3) limits.max_record_bytes = static_cast<std::size_t>(UINT32_MAX) + 1;
            try { RuntimeSendPath runtime(mode == 0 ? nullptr : plan, limits,
                        mode == 1 ? nullptr : std::make_shared<ControlledGenerationSource>()); }
            catch (const std::invalid_argument&) { rejected = true; }
            CHECK(rejected);
        }
        auto runtime = SendTestRuntime(); rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        const auto targets = std::vector<Locator>{SendTestLocator(10)};
        const std::uint8_t byte = 1;
        CHECK(!SendTest(sender, targets, {{nullptr, 1}}));
        CHECK(!SendTest(sender, targets, {{&byte, 1}}, 2));
        CHECK(!SendTest(sender, targets, {{&byte, 1}}, 0));
        CHECK(!SendTest(sender, targets, {{&byte, 1}}, 1025));
        CHECK(!SendTest(sender, targets, {}, 1, 0, SendQueue::Clock::now()));
        const auto direct = runtime->Submit({{&byte, 1}}, 1, nullptr, nullptr, SendQueue::Clock::now() + 1s, 0);
        CHECK(direct.status == RuntimeSendStatus::InvalidInput && runtime->SnapshotUsage().records == 0);
    });
    cases.emplace_back("DSP-10-attach-identity-release-entry-and-overlap", [] {
        auto runtime = SendTestRuntime(); rtps::SendResourceList a, b;
        auto& first = AttachSendTest(a, runtime); auto* original = a.front().get();
        CHECK(DSoftBusSenderResource::Attach(a, runtime).status == SenderAttachStatus::AlreadyAttached);
        CHECK(a.size() == 1 && a.front().get() == original);
        auto& other = AttachSendTest(b, runtime); CHECK(&other != &first);
        auto overlapping = SendTestRuntime();
        CHECK(overlapping->identity() != runtime->identity());
        CHECK(DSoftBusSenderResource::Attach(a, overlapping).status == SenderAttachStatus::OverlappingOwnership);
        CHECK(first.Release() && !first.Release());
        CHECK(DSoftBusSenderResource::Attach(a, runtime).status == SenderAttachStatus::ReleasedEntryPresent);
        CHECK(!SendTest(first, {SendTestLocator(10)}) && first.last_report()->status == RuntimeSendStatus::Released);
        CHECK(SendTest(other, {SendTestLocator(10)})); CHECK(runtime->TryTake());
        a.clear(); CHECK(DSoftBusSenderResource::Attach(a, runtime).status == SenderAttachStatus::Attached);
        CHECK(DSoftBusSenderResource::Attach(a, nullptr).status == SenderAttachStatus::InvalidRuntime);
        auto empty = SendTestRuntime(SendTestPlan({}));
        CHECK(DSoftBusSenderResource::Attach(a, empty).status == SenderAttachStatus::NoEgressRoutes);
        runtime->Close(); CHECK(DSoftBusSenderResource::Attach(a, runtime).status == SenderAttachStatus::RuntimeClosed);
    });
    cases.emplace_back("DSP-11-disjoint-runtimes-consume-complete-range-independently", [] {
        auto one_config = SendTestConfig(); one_config.routes.resize(1);
        auto two_config = SendTestConfig(); two_config.routes.erase(two_config.routes.begin());
        auto one = SendTestRuntime(SendTestPlan(one_config)), two = SendTestRuntime(SendTestPlan(two_config));
        rtps::SendResourceList list; auto& first = AttachSendTest(list, one); auto& second = AttachSendTest(list, two);
        const std::vector<Locator> all{SendTestLocator(11), SendTestLocator(10), SendTestLocator(12)};
        CHECK(SendTest(first, all)); CHECK(SendTest(second, all)); // Fresh begin/end on each call.
        auto a = one->TryTake(), b = two->TryTake(); CHECK(a && b && !one->TryTake() && !two->TryTake());
        CHECK(a->route().key.remote_locator == SendTestLocator(10) && b->route().key.remote_locator == SendTestLocator(11));
    });
    cases.emplace_back("DSP-11b-foreign-resources-are-not-owned-by-kind", [] {
        class ForeignSenderForTest final : public rtps::SenderResource {
        public:
            explicit ForeignSenderForTest(std::int32_t kind) : SenderResource(kind) {
                send_lambda_ = [](const auto&, auto, auto*, auto*, const auto&, auto) { return true; };
            }
        };
        rtps::SendResourceList list;
        list.push_back(std::make_unique<ForeignSenderForTest>(1));
        list.push_back(std::make_unique<ForeignSenderForTest>(LocatorCodec::kind));
        auto* udp = list[0].get(); auto* same_kind = list[1].get();
        auto runtime = SendTestRuntime(); auto& sender = AttachSendTest(list, runtime);
        CHECK(list.size() == 3 && list[0].get() == udp && list[1].get() == same_kind);
        CHECK(DSoftBusSenderResource::Attach(list, runtime).status == SenderAttachStatus::AlreadyAttached);
        sender.Release(); CHECK(SendTest(*udp, {}) && SendTest(*same_kind, {}));
    });
    cases.emplace_back("DSP-12-explicit-local-selection-and-advertisement", [] {
        auto config = SendTestConfig(true); config.routes.push_back(SendTestRoute(2, 11, 8));
        auto built = EgressPlan::Build(config, {{SendTestLocator(10), SendTestLocator(3), 7},
                {SendTestLocator(11), SendTestLocator(2), 8}}); CHECK(built.value);
        auto runtime = SendTestRuntime(built.value); rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        CHECK(SendTest(sender, {SendTestLocator(10), SendTestLocator(11)}));
        auto first = runtime->TryTake(), second = runtime->TryTake();
        CHECK(first && second && first->route().key.local_locator == SendTestLocator(3));
        CHECK(second->route().key.local_locator == SendTestLocator(2) && second->route().route_version == 8);
        rtps::LocatorList_t locals; sender.add_locators_to_list(locals); sender.add_locators_to_list(locals);
        CHECK(locals.size() == 2);
        CHECK(std::find(locals.begin(), locals.end(), SendTestLocator(2)) != locals.end());
        CHECK(std::find(locals.begin(), locals.end(), SendTestLocator(3)) != locals.end());
        sender.Release(); rtps::LocatorList_t after; sender.add_locators_to_list(after); CHECK(after.empty());
    });
    cases.emplace_back("DSP-13-release-destruction-exactly-once-and-inflight", [] {
        std::atomic<unsigned> releases{0}; std::optional<OwnedSendRecord> held;
        {
            auto runtime = SendTestRuntime(); rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
            RuntimeSendPathTestAccess::OnRelease(sender, [&] { ++releases; });
            CHECK(SendTest(sender, {SendTestLocator(10)})); held = runtime->TryTake(); CHECK(held);
            CHECK(sender.Release()); CHECK(!sender.Release()); CHECK(releases == 1);
            CHECK(runtime->SnapshotUsage().records == 1 && held->data()[0] == 42);
            list.clear(); CHECK(releases == 1);
            auto& fresh = AttachSendTest(list, runtime);
            RuntimeSendPathTestAccess::OnRelease(fresh, [&] { ++releases; });
            CHECK(SendTest(fresh, {SendTestLocator(11)}));
        }
        CHECK(releases == 2 && held && held->generation() == 1005 && held->data()[0] == 42);
        held.reset();
    });
    cases.emplace_back("DSP-14-release-waits-for-entered-send-no-late-publish", [] {
        SendTestEvent copying, allow_copy, release_entered; std::atomic<unsigned> copies{0}, releases{0};
        const auto now = SendQueue::Clock::now(); auto limits = SendTestLimits();
        auto queue = SendQueueTestAccess::Make(limits, [now] { return now; }, [&] {
            if (copies.fetch_add(1) == 0) { copying.Signal(); allow_copy.Wait(); }
        });
        auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), limits,
                std::make_shared<ControlledGenerationSource>(), std::move(queue), [now] { return now; });
        rtps::SendResourceList first_list, second_list;
        auto& sender = AttachSendTest(first_list, runtime); auto& other = AttachSendTest(second_list, runtime);
        RuntimeSendPathTestAccess::OnRelease(sender, [&] { ++releases; });
        auto send = std::async(std::launch::async, [&] { return SendTest(sender, {SendTestLocator(10)}, {}, 1, 0, now + 1s); });
        copying.Wait();
        auto release = std::async(std::launch::async, [&] { release_entered.Signal(); return sender.Release(); });
        release_entered.Wait();
        const bool release_pending = release.wait_for(0ms) != std::future_status::ready;
        const auto during = runtime->SnapshotUsage(); const auto release_count = releases.load();
        allow_copy.Signal(); const auto sent = Finish(send); const auto closed = Finish(release);
        CHECK(sent && closed && release_pending && release_count == 0 && releases == 1);
        CHECK(during.records == 1 && during.queued == 0);
        auto accepted_before_release = runtime->TryTake(); CHECK(accepted_before_release);
        CHECK(!SendTest(sender, {SendTestLocator(10)}) && !runtime->TryTake());
        CHECK(sender.last_report()->status == RuntimeSendStatus::Released);
        CHECK(SendTest(other, {SendTestLocator(11)})); CHECK(runtime->TryTake());
    });
    cases.emplace_back("DSP-15-runtime-close-during-batch-copy", [] {
        SendTestEvent copying, allow_copy; const auto now = SendQueue::Clock::now(); auto limits = SendTestLimits();
        auto queue = SendQueueTestAccess::Make(limits, [now] { return now; }, [&] { copying.Signal(); allow_copy.Wait(); });
        auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), limits,
                std::make_shared<ControlledGenerationSource>(), std::move(queue), [now] { return now; });
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        auto send = std::async(std::launch::async, [&] {
            return SendTest(sender, {SendTestLocator(10), SendTestLocator(11)}, {}, 1, 0, now + 1s);
        });
        copying.Wait(); const auto reserved = runtime->SnapshotUsage(); runtime->Close();
        CHECK(runtime->SnapshotUsage().closed && !runtime->TryTake());
        allow_copy.Signal(); // Gate used for each target: opened event remains signalled.
        CHECK(!Finish(send) && reserved.records == 2 && reserved.queued == 0);
        CHECK(sender.last_report()->queue_result == SendAdmission::Closed);
        CHECK(runtime->SnapshotUsage().records == 0 && !runtime->TryTake());
        CHECK(!SendTest(sender, {SendTestLocator(10)}));
        CHECK(sender.last_report()->status == RuntimeSendStatus::RuntimeClosed);
    });
    cases.emplace_back("DSP-16-attach-close-publication-race", [] {
        SendTestEvent staged, allow_commit; const auto now = SendQueue::Clock::now(); auto limits = SendTestLimits();
        auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), limits,
                std::make_shared<ControlledGenerationSource>(), nullptr, [now] { return now; },
                [&] { staged.Signal(); allow_commit.Wait(); });
        rtps::SendResourceList list;
        auto attach = std::async(std::launch::async, [&] { return DSoftBusSenderResource::Attach(list, runtime); });
        staged.Wait(); runtime->Close(); allow_commit.Signal();
        CHECK(Finish(attach).status == SenderAttachStatus::RuntimeClosed && list.empty());
        CHECK(DSoftBusSenderResource::Attach(list, runtime).status == SenderAttachStatus::RuntimeClosed);
    });
    cases.emplace_back("DSP-17-every-concurrent-close-crosses-queue-barrier", [] {
        SendTestEvent first_request, allow_first; std::atomic<unsigned> close_calls{0};
        auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), SendTestLimits(),
                std::make_shared<ControlledGenerationSource>(), nullptr, {}, {}, [&] {
                    if (close_calls.fetch_add(1) == 0) { first_request.Signal(); allow_first.Wait(); }
                });
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        CHECK(SendTest(sender, {SendTestLocator(10)}));
        auto first = std::async(std::launch::async, [&] { runtime->Close(); return true; });
        first_request.Wait();
        auto second = std::async(std::launch::async, [&] { runtime->Close(); return true; });
        const bool second_done = Finish(second); const auto after_second = runtime->SnapshotUsage();
        allow_first.Signal(); CHECK(Finish(first));
        CHECK(second_done && after_second.closed && after_second.queued == 0 && after_second.records == 0);
    });
    cases.emplace_back("DSP-18-original-deadline-across-resource-gate", [] {
        const auto base = SendQueue::Clock::now() + 1h;
        std::atomic<int> offset{0}; std::atomic<unsigned> copies{0};
        SendTestEvent copying, allow_copy, waiting_gate;
        static thread_local bool notify_gate = false;
        const auto clock = [&] {
            if (notify_gate) { notify_gate = false; waiting_gate.Signal(); }
            return base + std::chrono::milliseconds(offset.load());
        };
        auto limits = SendTestLimits(); auto queue = SendQueueTestAccess::Make(limits, clock, [&] {
            if (copies.fetch_add(1) == 0) { copying.Signal(); allow_copy.Wait(); }
        });
        auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), limits,
                std::make_shared<ControlledGenerationSource>(), std::move(queue), clock);
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        auto first = std::async(std::launch::async, [&] {
            return SendTest(sender, {SendTestLocator(10)}, {}, 1, 0, base + 1s);
        });
        copying.Wait();
        auto second = std::async(std::launch::async, [&] {
            notify_gate = true; return SendTest(sender, {SendTestLocator(11)}, {}, 1, 0, base + 5ms);
        });
        waiting_gate.Wait(); offset = 6; allow_copy.Signal();
        CHECK(Finish(first) && !Finish(second));
        auto held = runtime->TryTake(); CHECK(held && held->deadline() == base + 1s);
        CHECK(!runtime->TryTake() && sender.last_report()->status == RuntimeSendStatus::DeadlineExpired);
    });
    cases.emplace_back("DSP-19-preparation-deadline-and-queue-only-cap", [] {
        const auto base = SendQueue::Clock::now() + 1h; std::atomic<int> offset{0};
        const auto clock = [&] { return base + std::chrono::milliseconds(offset.load()); };
        auto source = std::make_shared<ControlledGenerationSource>([&](const Route& route, SendConnectionSnapshot& output) {
            output = {route.key, route.route_version, 7}; offset = 25; return true;
        });
        auto limits = SendTestLimits(); auto runtime = RuntimeSendPathTestAccess::Make(SendTestPlan(), limits,
                source, SendQueueTestAccess::Make(limits, clock), clock);
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        CHECK(!SendTest(sender, {SendTestLocator(10)}, {}, 1, 3, base + 20ms));
        CHECK(sender.last_report()->status == RuntimeSendStatus::DeadlineExpired && !runtime->TryTake());
        offset = 0;
        // Preparation consumes 25 ms of the original 100 ms, not a new deadline.
        // Queue admission starts at 25 ms and preserves the original 100 ms.
        CHECK(SendTest(sender, {SendTestLocator(10)}, {}, 1, 9, base + 100ms));
        auto record = runtime->TryTake(); CHECK(record && record->deadline() == base + 100ms && record->priority() == 9);
    });
    cases.emplace_back("DSP-20-second-target-full-is-atomic-through-real-sender", [] {
        auto runtime = SendTestRuntime(SendTestPlan(), std::make_shared<ControlledGenerationSource>(), {1, 8, 4, 16, 8});
        rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
        CHECK(SendTest(sender, {SendTestLocator(11)})); auto full = runtime->TryTake(); CHECK(full);
        CHECK(!SendTest(sender, {SendTestLocator(10), SendTestLocator(11)}));
        CHECK(sender.last_report()->queue_result == SendAdmission::DeadlineExpired);
        CHECK(!runtime->TryTake() && runtime->SnapshotUsage().records == 1 && runtime->SnapshotUsage().peers == 1);
    });
    cases.emplace_back("DSP-21-actual-submit-allocation-failure-sweep", [] {
        auto plan = SendTestPlan(); auto source = std::make_shared<ControlledGenerationSource>();
        const auto now = SendQueue::Clock::now(); const auto clock = [now] { return now; };
        const std::vector<Locator> targets{SendTestLocator(10), SendTestLocator(11)};
        const std::uint8_t data[]{1, 2, 3}; const std::vector<rtps::NetworkBuffer> buffers{{data, 1}, {data + 1, 2}};
        std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            auto limits = SendTestLimits(); auto runtime = RuntimeSendPathTestAccess::Make(plan, limits, source,
                    SendQueueTestAccess::Make(limits, clock), clock);
            rtps::SendResourceList list; auto& sender = AttachSendTest(list, runtime);
            rtps::Locators begin(targets.cbegin()), end(targets.cend());
            bool accepted; std::size_t attempted, failed;
            {
                SendAllocationScope fail(iteration == 0 ? (std::numeric_limits<std::size_t>::max)() : iteration - 1);
                accepted = sender.send(buffers, 3, &begin, &end, now + 1s, 8);
                attempted = send_allocation_index; failed = send_allocation_failures;
            }
            if (iteration == 0) { CHECK(accepted && failed == 0 && attempted > 4); count = attempted; }
            else {
                CHECK(!accepted && failed == 1 && attempted == iteration);
                CHECK(!runtime->TryTake() && runtime->SnapshotUsage().records == 0 && runtime->SnapshotUsage().peers == 0);
                const auto report = sender.last_report(); CHECK(report);
                CHECK(report->status == RuntimeSendStatus::PreparationFailed ||
                        (report->status == RuntimeSendStatus::QueueRejected && report->queue_result == SendAdmission::CopyFailed));
                CHECK(SendTest(sender, targets, buffers, 3, 8, now + 1s));
            }
            if (iteration == count) break;
        }
        std::cout << "submit allocation failure points=" << count << std::endl;
    });
    cases.emplace_back("DSP-22-actual-attach-allocation-failure-sweep", [] {
        auto runtime = SendTestRuntime(); std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            rtps::SendResourceList list;
            SenderAttachResult result; std::size_t attempted, failed;
            {
                SendAllocationScope fail(iteration == 0 ? (std::numeric_limits<std::size_t>::max)() : iteration - 1);
                result = DSoftBusSenderResource::Attach(list, runtime);
                attempted = send_allocation_index; failed = send_allocation_failures;
            }
            if (iteration == 0) { CHECK(result.ok() && failed == 0 && attempted >= 2); count = attempted; }
            else {
                CHECK(!result.ok() && result.status == SenderAttachStatus::AllocationFailure && failed == 1 && attempted == iteration);
                CHECK(list.empty() && runtime->SnapshotUsage().records == 0);
                CHECK(DSoftBusSenderResource::Attach(list, runtime).ok());
            }
            if (iteration == count) break;
        }
        std::cout << "attach allocation failure points=" << count << std::endl;
    });
    cases.emplace_back("DSP-23-egress-complete-real-allocation-sweep", [] {
        const auto config = SendTestConfig(); const std::vector<EgressSelection> selections;
        std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            EgressBuildResult result; std::size_t attempted, failed;
            {
                SendAllocationScope fail(iteration == 0 ? (std::numeric_limits<std::size_t>::max)() : iteration - 1);
                result = EgressPlan::Build(config, selections); attempted = send_allocation_index; failed = send_allocation_failures;
            }
            if (iteration == 0) { CHECK(result.value && failed == 0 && attempted > 0); count = attempted; }
            else {
                CHECK(!result.value && result.issue.code == EgressError::AllocationFailure && failed == 1 && attempted == iteration);
                CHECK(EgressPlan::Build(config, selections).value);
            }
            if (iteration == count) break;
        }
        std::cout << "egress allocation failure points=" << count << std::endl;
    });
    for (const std::string mode : {"build", "routes", "update"}) {
        cases.emplace_back("DSP-24-directory-" + mode + "-real-allocation-sweep", [mode] {
            const auto count = DirectoryAllocationProbe(mode, (std::numeric_limits<std::size_t>::max)());
            for (std::size_t at = 0; at < count; ++at) DirectoryAllocationProbe(mode, at);
            std::cout << "directory " << mode << " allocation failure points=" << count << std::endl;
        });
    }
    std::size_t failures = 0, executed = 0;
    for (const auto& test : cases) {
        if (argc == 2 && test.first != argv[1]) continue;
        ++executed;
        try { test.second(); std::cout << "PASS " << test.first << std::endl; }
        catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << std::endl; }
    }
    std::cout << "RuntimeSendPath groups=" << executed << " failures=" << failures << std::endl;
    return executed != 0 && failures == 0 ? 0 : 1;
}
int main(int argc, char** argv)
{
    // A noexcept allocation failure must be an observable child-process failure,
    // not a Windows abort dialog that hangs the unattended test process.
    std::set_terminate([] { std::fputs("FAIL terminate\n", stderr); std::_Exit(86); });
    return RunRuntimeSendPathTests(argc, argv);
}
