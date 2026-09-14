#include <mdds/dsoftbus/DSoftBusTransport.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include "transport_facade_selector_cases.hpp"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define F_DUP _dup
#define F_DUP2 _dup2
#define F_FILENO _fileno
#define F_CLOSE _close
#else
#include <unistd.h>
#define F_DUP dup
#define F_DUP2 dup2
#define F_FILENO fileno
#define F_CLOSE close
#endif

#if !defined(FASTDDS_DYN_LINK) || defined(FASTDDS_STATIC_LINK)
#error Facade tests require the real shared Fast DDS CONFIG binding.
#endif

using namespace mdds::dsoftbus;
namespace rtps = eprosima::fastdds::rtps;
using namespace std::chrono_literals;
#define FCHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string("line ") + \
    std::to_string(__LINE__) + ": " #condition); } while (false)
thread_local bool facade_alloc_armed = false;
thread_local std::size_t facade_alloc_index = 0, facade_fail_at = SIZE_MAX, facade_alloc_failures = 0;
void* operator new(std::size_t size) {
    if (facade_alloc_armed && facade_alloc_index++ == facade_fail_at) { ++facade_alloc_failures; throw std::bad_alloc(); }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace mdds::dsoftbus {
class DSoftBusTransportTestAccess {
public:
    static std::unique_ptr<DSoftBusTransport> CreateHostModel(const DSoftBusTransportDescriptor& d,
            std::shared_ptr<const ConnectionGenerationSource> source) {
        return std::unique_ptr<DSoftBusTransport>(new DSoftBusTransport(d, DSoftBusTransport::HostModelTag{}, std::move(source)));
    }
    static InputChannelTicket Capture(DSoftBusTransport& t, const Locator& l) {
        auto ready = t.ReadyLease();
        if (!ready) return {};
        auto* slot = ready->FindLocal(l);
        return slot && slot->committed_open.load() ? ready->inputs->Capture(l) : InputChannelTicket{};
    }
    static InputDeliveryResult Deliver(DSoftBusTransport& t, const InputChannelTicket& ticket,
            const Locator& remote, const void* bytes, std::size_t size) {
        auto ready = t.ReadyLease();
        return ready ? ready->inputs->Deliver(ticket, remote, bytes, size) : InputDeliveryResult::Disabled;
    }
    static std::shared_ptr<RuntimeSendPath> Runtime(DSoftBusTransport& t) {
        auto ready = t.ReadyLease(); return ready ? ready->runtime : nullptr;
    }
    static InputChannelUsage Usage(DSoftBusTransport& t) {
        std::lock_guard<std::mutex> lock(t.lifecycle_); return t.ready_ ? t.ready_->inputs->SnapshotUsage() : InputChannelUsage{};
    }
    static void Hooks(DSoftBusTransport& t, std::function<void()> open = {}, std::function<void()> close = {},
            std::function<void()> shutdown = {}, std::function<void()> publish = {}) {
        t.hooks_ = {std::move(publish), std::move(open), std::move(close), std::move(shutdown)};
    }
};
class SendQueueTestAccess {
public:
    static std::unique_ptr<SendQueue> Make(SendQueueLimits limits, std::function<void()> copying) {
        return std::unique_ptr<SendQueue>(new SendQueue(limits, {{}, {}, std::move(copying), {}}));
    }
};
class RuntimeSendPathTestAccess {
public:
    // Before any sender/consumer is published by the test; never a production injection API.
    static void ReplaceQueue(RuntimeSendPath& runtime, std::unique_ptr<SendQueue> queue) { runtime.queue_ = std::move(queue); }
};
}
namespace {
Locator FL(std::uint8_t id) {
    EndpointId endpoint{}; endpoint[15] = id; Locator result; FCHECK(LocatorCodec::Encode(endpoint, result)); return result;
}
Route FR(std::uint8_t local, std::uint8_t remote, std::uint64_t version) {
    Route result; result.key = {FL(local), FL(remote)}; result.route_version = version;
    result.fields = {"net-" + std::to_string(remote), "listen-" + std::to_string(remote),
        "dial-" + std::to_string(local) + "-" + std::to_string(remote),
        {"peer-" + std::to_string(remote) + "-" + std::to_string(local)}, RecordDataType::Bytes}; return result;
}
DSoftBusTransportDescriptor FD() {
    DSoftBusTransportDescriptor d(64);
    d.directory.locals = {{FL(2), "ns-a", "local-a"}, {FL(3), "ns-b", "local-b"}};
    d.directory.routes = {FR(2, 10, 5), FR(3, 11, 6)};
    d.default_metatraffic_unicast = {FL(2)}; d.default_user_unicast = {FL(3)}; return d;
}
class FacadeControlledGenerationSource final : public ConnectionGenerationSource {
public:
    bool TryResolve(const Route& r, SendConnectionSnapshot& out) const override {
        ++calls; out = {r.key, r.route_version, r.route_version + 100}; return true;
    }
    mutable std::atomic<unsigned> calls{0};
};
std::unique_ptr<DSoftBusTransport> FM(const DSoftBusTransportDescriptor& d = FD()) {
    return DSoftBusTransportTestAccess::CreateHostModel(d, std::make_shared<FacadeControlledGenerationSource>());
}
class FacadeReceiver final : public rtps::TransportReceiverInterface {
public:
    void OnDataReceived(const rtps::octet* data, const std::uint32_t size, const Locator& local, const Locator& remote) override {
        if (callback) callback();
        bytes.assign(data, data + size); seen_local = local; seen_remote = remote; ++calls;
    }
    std::function<void()> callback;
    std::vector<std::uint8_t> bytes;
    Locator seen_local, seen_remote;
    std::atomic<unsigned> calls{0};
};
class FacadeEvent {
public:
    void Signal() { std::lock_guard<std::mutex> lock(mutex); ++count; changed.notify_all(); }
    void Wait(std::size_t required = 1) {
        std::unique_lock<std::mutex> lock(mutex);
        if (!changed.wait_for(lock, 3s, [&] { return count >= required; })) { std::fputs("FAIL event watchdog\n", stderr); std::_Exit(87); }
    }
private:
    std::mutex mutex; std::condition_variable changed; std::size_t count = 0;
};
template<class T> T FFinish(std::future<T>& work) {
    if (work.wait_for(3s) != std::future_status::ready) { std::fputs("FAIL future watchdog\n", stderr); std::_Exit(88); }
    return work.get();
}
bool FSend(rtps::SenderResource& sender, const std::vector<Locator>& targets, std::int32_t priority = 7, bool legacy = false) {
    const std::uint8_t byte = 42; const std::vector<rtps::NetworkBuffer> buffers{{&byte, 1}};
    rtps::Locators begin(targets.begin()), end(targets.end()); const auto deadline = SendQueue::Clock::now() + 1s;
    return legacy ? sender.send(buffers, 1, &begin, &end, deadline) : sender.send(buffers, 1, &begin, &end, deadline, priority);
}
}

int RunTransportFacadeTests(int argc, char** argv) {
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    cases.emplace_back("FCD-01-production-factory", [] {
        auto d = FD(); std::unique_ptr<rtps::TransportInterface> a(d.create_transport()), b(d.create_transport());
        FCHECK(a && b && a.get() != b.get() && d.min_send_buffer_size() == 64 && d.maxInitialPeersRange == 1);
        auto* typed = dynamic_cast<DSoftBusTransport*>(a.get()); FCHECK(typed && typed->mode() == FacadeMode::NativeUnavailable);
        FCHECK(!a->init() && typed->last_error().error == FacadeError::NativeBackendUnavailable && typed->state() == FacadeState::Failed);
        FCHECK(!a->init() && typed->state() == FacadeState::Failed);
        d.maxMessageSize = 0; std::unique_ptr<rtps::TransportInterface> invalid(d.create_transport());
        FCHECK(invalid && !invalid->init());
        FCHECK(dynamic_cast<DSoftBusTransport*>(invalid.get())->last_error().error == FacadeError::InvalidLimits);
    });
    cases.emplace_back("FCD-02-explicit-host-model", [] {
        auto model = FM(); FCHECK(model->mode() == FacadeMode::HostModel && model->init());
        FCHECK(model->state() == FacadeState::ModelReady && model->max_recv_buffer_size() == 64);
        auto no_source = DSoftBusTransportTestAccess::CreateHostModel(FD(), nullptr);
        FCHECK(!no_source->init() && no_source->state() == FacadeState::Failed);
    });
    cases.emplace_back("FCD-03-frozen-configuration", [] {
        auto d = FD(); auto model = FM(d); d.directory.routes.clear(); d.maxMessageSize = 1;
        FCHECK(model->init()); auto* view = dynamic_cast<DSoftBusTransportDescriptor*>(model->get_configuration());
        FCHECK(view && view == model->get_configuration()); view->maxMessageSize = 2; view->directory.routes.clear();
        view->default_metatraffic_unicast.clear();
        FCHECK(model->max_recv_buffer_size() == 64 && model->RemoteToMainLocal(FL(10)) == FL(2));
        rtps::LocatorList locals; FCHECK(model->getDefaultMetatrafficUnicastLocators(locals, 999) && locals.size() == 1);
        FCHECK(!model->init() && model->state() == FacadeState::ModelReady);
        model->shutdown(); FCHECK(!model->init() && model->state() == FacadeState::Closed);
    });
    cases.emplace_back("FCD-04-value-validation", [] {
        for (int fault = 0; fault < 12; ++fault) {
            auto d = FD(); FacadeError expected = FacadeError::InvalidLimits;
            if (fault == 0) d.maxMessageSize = 0;
            if (fault == 1) d.send_limits.max_record_bytes = 65;
            if (fault == 2) d.send_limits.peer_records = 0;
            if (fault == 3) d.send_limits.global_bytes = 63;
            if (fault == 4) d.maxInitialPeersRange = 4;
            if (fault == 5) d.send_limits.max_record_bytes = SIZE_MAX;
            if (fault == 6) { d.default_user_unicast.clear(); expected = FacadeError::InvalidDefaultLocal; }
            if (fault == 7) { d.default_metatraffic_unicast = {FL(10)}; expected = FacadeError::InvalidDefaultLocal; }
            if (fault == 8) { d.default_user_unicast = {FL(3), FL(3)}; expected = FacadeError::InvalidDefaultLocal; }
            if (fault == 9) { d.directory.routes.push_back(FR(3, 10, 8)); expected = FacadeError::EgressRejected; }
            if (fault == 10) { d.egress_selections = {{FL(10), FL(2), 4}}; expected = FacadeError::EgressRejected; }
            if (fault == 11) { d.directory.routes.front().route_version = 0; expected = FacadeError::DirectoryRejected; }
            auto t = FM(d); FCHECK(!t->init() && t->state() == FacadeState::Failed && t->last_error().error == expected);
            rtps::SendResourceList senders; FCHECK(!t->OpenOutputChannel(senders, FL(10)) && senders.empty());
        }
        auto t = FM(); FCHECK(!t->init(nullptr, 65) && t->last_error().error == FacadeError::UnsupportedNoFragmentRequest);
        auto permitted = FM(); FCHECK(permitted->init(nullptr, 64));
        auto receive_only = FD(); receive_only.directory.routes.clear(); auto r = FM(receive_only); FCHECK(r->init());
        rtps::SendResourceList empty; FCHECK(!r->OpenOutputChannel(empty, FL(2)) && empty.empty());
    });
    cases.emplace_back("FCD-04b-real-model-construction-allocation-sweep", [] {
        auto d = FD(); const auto source = std::make_shared<FacadeControlledGenerationSource>();
        // Long, legal strings exercise real payload allocations, not only vector
        // bookkeeping or a Debug STL implementation's iterator proxies.
        for (auto& local : d.directory.locals) {
            local.callback_namespace.append(40, 'n'); local.local_listen_name.append(40, 'l');
        }
        for (auto& route : d.directory.routes) {
            route.fields.remote_network_id.append(32, 'n'); route.fields.remote_listen_name.append(40, 'r');
            route.fields.local_initiator_name.append(40, 'i'); route.fields.expected_remote_initiator_names.front().append(40, 'p');
        }
        std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            std::unique_ptr<DSoftBusTransport> t; bool success = false, thrown = false;
            facade_alloc_index = 0; facade_alloc_failures = 0; facade_fail_at = iteration == 0 ? SIZE_MAX : iteration - 1;
            facade_alloc_armed = true;
            try { t = DSoftBusTransportTestAccess::CreateHostModel(d, source); success = t->init(); }
            catch (const std::bad_alloc&) { thrown = true; }
            facade_alloc_armed = false;
            if (iteration == 0) { FCHECK(success && !thrown && facade_alloc_failures == 0); count = facade_alloc_index; }
            else {
                FCHECK(!success && facade_alloc_failures == 1 && facade_alloc_index == iteration);
                if (t) FCHECK(!thrown && t->state() == FacadeState::Failed && t->last_error().error == FacadeError::AllocationFailure);
                else FCHECK(thrown);
                auto next = FM(d); FCHECK(next->init());
            }
            if (iteration == count) break;
        }
        std::cout << "facade model allocation failure points=" << count << std::endl;
    });
    cases.emplace_back("FCD-04c-production-factory-allocation-failure", [] {
        const auto d = FD(); std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            rtps::TransportInterface* created = nullptr;
            facade_alloc_index = 0; facade_alloc_failures = 0; facade_fail_at = iteration == 0 ? SIZE_MAX : iteration - 1;
            facade_alloc_armed = true; created = d.create_transport(); facade_alloc_armed = false;
            std::unique_ptr<rtps::TransportInterface> owner(created);
            if (iteration == 0) { FCHECK(owner && facade_alloc_failures == 0); count = facade_alloc_index; }
            else FCHECK(!owner && facade_alloc_failures == 1 && facade_alloc_index == iteration);
            if (iteration == count) break;
        }
        FCHECK(count > 0); std::cout << "facade production factory allocation failure points=" << count << std::endl;
    });
    cases.emplace_back("FCD-04d-failed-publication-does-not-publish-ready", [] {
        auto t = FM(); DSoftBusTransportTestAccess::Hooks(*t, {}, {}, {}, [] { throw std::bad_alloc(); });
        FCHECK(!t->init() && t->last_error().error == FacadeError::AllocationFailure && t->state() == FacadeState::Failed);
        FCHECK(!DSoftBusTransportTestAccess::Runtime(*t) && !t->IsInputChannelOpen(FL(2)));
        rtps::SendResourceList list; FCHECK(!t->OpenOutputChannel(list, FL(10)) && list.empty());
    });
    cases.emplace_back("FCD-04e-shutdown-during-init-cannot-revive", [] {
        for (const bool fail_after_shutdown : {false, true}) {
            auto t = FM(); FacadeEvent prepared, allow;
            DSoftBusTransportTestAccess::Hooks(*t, {}, {}, {}, [&] {
                prepared.Signal(); allow.Wait();
                if (fail_after_shutdown) throw std::bad_alloc();
            });
            auto initializing = std::async(std::launch::async, [&] { return t->init(); });
            prepared.Wait(); t->shutdown();
            const bool closed_before_resume = t->state() == FacadeState::Closed;
            allow.Signal(); const bool initialized = FFinish(initializing);
            FCHECK(closed_before_resume && !initialized && t->state() == FacadeState::Closed);
            FCHECK(!DSoftBusTransportTestAccess::Runtime(*t));
            rtps::SendResourceList list; FCHECK(!t->OpenOutputChannel(list, FL(10)) && list.empty());
        }
    });
    cases.emplace_back("FCD-05-locator-matrix", [] {
        auto t = FM(); FCHECK(t->init());
        for (int item = 0; item < 10; ++item) {
            auto l = item == 0 ? FL(2) : item == 1 ? FL(10) : FL(12);
            if (item == 3) l.kind = LOCATOR_KIND_UDPv4;
            if (item == 4) l.port = 0;
            if (item == 5) l.port = 2;
            if (item >= 6) { std::fill(std::begin(l.address), std::end(l.address), std::uint8_t{0}); }
            if (item == 7) l.address[15] = 1;
            if (item == 8) l.address[0] = 0xff;
            if (item == 9) l.address[15] = 10;
            const bool supported = item <= 2 || item == 9, allowed = item < 2 || item == 9, remote = item == 1 || item == 9;
            FCHECK(t->IsLocatorSupported(l) == supported && t->is_locator_allowed(l) == allowed);
            FCHECK(t->is_local_locator(l) == (item == 0) && t->is_locator_reachable(l) == remote);
            FCHECK(t->DoInputLocatorsMatch(l, l) == (item == 0));
            auto normalized = t->NormalizeLocator(l); FCHECK(normalized.size() == (allowed ? 1u : 0u));
            Locator out = FL(3); const Locator before = out; rtps::TransportInterface& base = *t;
            FCHECK(base.transform_remote_locator(l, out, true, true) == remote);
            FCHECK(out == (remote ? l : before));
            FCHECK(t->RemoteToMainLocal(l) == (remote ? FL(2) : Locator{}));
            auto fill = l; FCHECK(t->fillUnicastLocator(fill, 7500) == (item == 0) && fill == l);
            FCHECK(t->fillMetatrafficUnicastLocator(fill, 7500) == (item == 0) && fill == l);
        }
        FCHECK(!t->DoInputLocatorsMatch(FL(2), FL(3)) && !t->is_localhost_allowed());
    });
    cases.emplace_back("FCD-06-defaults-and-explicit-initial-peers", [] {
        auto t = FM(); FCHECK(t->init()); rtps::LocatorList meta, user, output;
        FCHECK(t->getDefaultMetatrafficUnicastLocators(meta, 1234) && t->getDefaultUnicastLocators(user, 9876));
        FCHECK(meta.size() == 1 && *meta.begin() == FL(2) && user.size() == 1 && *user.begin() == FL(3));
        FCHECK(t->getDefaultUnicastLocators(user, 1234) && user.size() == 1);
        t->AddDefaultOutputLocator(output); t->AddDefaultOutputLocator(output); FCHECK(output.size() == 2);
        auto sentinel = meta; FCHECK(!t->getDefaultMetatrafficMulticastLocators(meta, 42) && meta == sentinel);
        auto local = FL(2); FCHECK(!t->fillMetatrafficMulticastLocator(local, 42) && local == FL(2));
        rtps::LocatorList peers; auto remote = FL(10); rtps::PortParameters ports;
        FCHECK(t->configureInitialPeerLocator(remote, ports, 321, peers) && peers.size() == 1 && remote == FL(10));
        FCHECK(t->configureInitialPeerLocator(remote, ports, 0, peers) && peers.size() == 1);
        auto unknown = FL(12); const auto before = peers;
        FCHECK(!t->configureInitialPeerLocator(unknown, ports, 123, peers) && peers == before && unknown == FL(12));
    });
    cases.emplace_back("FCD-06b-output-list-allocation-visibility-probe", [] {
        auto t = FM(); FCHECK(t->init()); std::size_t count = 0;
        for (std::size_t iteration = 0;; ++iteration) {
            rtps::LocatorList list; list.push_back(FL(12)); const auto before = list;
            bool caught = false, success = false;
            facade_alloc_index = 0; facade_alloc_failures = 0; facade_fail_at = iteration == 0 ? SIZE_MAX : iteration - 1;
            facade_alloc_armed = true;
            try { success = t->getDefaultUnicastLocators(list, 999); } catch (const std::bad_alloc&) { caught = true; }
            facade_alloc_armed = false;
            if (iteration == 0) { FCHECK(success && !caught && list.size() == 2); count = facade_alloc_index; }
            else FCHECK(!success && caught && facade_alloc_failures == 1 && list == before);
            if (iteration == count) break;
        }
        std::cout << "facade output-list observed executable allocation failure points=" << count << std::endl;
        if (count == 0) std::cout << "SKIPPED DLL-internal LocatorList allocation injection: executable allocator observed no points" << std::endl;
    });
    cases.emplace_back("FCD-07-real-selector", [] { auto t = FM(); FCHECK(t->init()); RunFacadeSelectorCases(*t, FL(10), FL(11)); });
    cases.emplace_back("FCD-08-receiver-capacity-and-record-bound", [] {
        auto t = FM(); FCHECK(t->init()); FacadeReceiver receiver;
        FCHECK(!t->OpenInputChannel(FL(2), &receiver, 63));
        FCHECK(t->last_error().error == FacadeError::InsufficientReceiverCapacity && !t->IsInputChannelOpen(FL(2)));
        FCHECK(t->OpenInputChannel(FL(2), &receiver, 64) && t->OpenInputChannel(FL(2), &receiver, 128));
        const auto ticket = DSoftBusTransportTestAccess::Capture(*t, FL(2));
        std::vector<std::uint8_t> bytes(65, 19);
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(10), bytes.data(), 64) == InputDeliveryResult::Delivered);
        FCHECK(receiver.calls == 1 && receiver.bytes.size() == 64 && receiver.bytes.front() == 19);
        FCHECK(receiver.seen_local == FL(2) && receiver.seen_remote == FL(10));
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(10), bytes.data(), 65) == InputDeliveryResult::TooLarge);
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(12), bytes.data(), 1) == InputDeliveryResult::InvalidRoute);
        FCHECK(receiver.calls == 1); t->CloseInputChannel(FL(2));
    });
    cases.emplace_back("FCD-09-input-identity-and-idempotence", [] {
        auto t = FM(), other = FM(); FCHECK(t->init() && other->init()); FacadeReceiver one, two;
        FCHECK(t->OpenInputChannel(FL(2), &one, 64) && t->OpenInputChannel(FL(2), &one, 64));
        FCHECK(!t->OpenInputChannel(FL(2), &two, 64) && !t->OpenInputChannel(FL(3), &one, 64));
        FCHECK(t->OpenInputChannel(FL(3), &two, 64));
        const auto old = DSoftBusTransportTestAccess::Capture(*t, FL(2)); const std::uint8_t byte = 1;
        FCHECK(DSoftBusTransportTestAccess::Deliver(*other, old, FL(10), &byte, 1) == InputDeliveryResult::InvalidTicket);
        FCHECK(t->CloseInputChannel(FL(2)) && t->CloseInputChannel(FL(2)) && !t->CloseInputChannel(FL(12)));
        FCHECK(t->OpenInputChannel(FL(2), &one, 64));
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, old, FL(10), &byte, 1) == InputDeliveryResult::Closed);
        const auto fresh = DSoftBusTransportTestAccess::Capture(*t, FL(2));
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, fresh, FL(10), &byte, 1) == InputDeliveryResult::Delivered);
        t->shutdown();
    });
    cases.emplace_back("FCD-10-close-drain-and-replacement-control", [] {
        auto t = FM(); FCHECK(t->init()); FacadeReceiver old, replacement; FacadeEvent entered, allow, marked, replacement_started;
        old.callback = [&] { entered.Signal(); allow.Wait(); };
        DSoftBusTransportTestAccess::Hooks(*t, {}, [&] { marked.Signal(); });
        FCHECK(t->OpenInputChannel(FL(2), &old, 64)); const auto ticket = DSoftBusTransportTestAccess::Capture(*t, FL(2));
        const std::uint8_t byte = 1;
        auto receive = std::async(std::launch::async, [&] { return DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(10), &byte, 1); });
        entered.Wait(); auto close = std::async(std::launch::async, [&] { return t->CloseInputChannel(FL(2)); }); marked.Wait();
        const bool closed_too_early = close.wait_for(0ms) == std::future_status::ready;
        FCHECK(!t->IsInputChannelOpen(FL(2)));
        auto reopen = std::async(std::launch::async, [&] { replacement_started.Signal(); return t->OpenInputChannel(FL(2), &replacement, 64); });
        replacement_started.Wait(); const bool opened_too_early = reopen.wait_for(0ms) == std::future_status::ready;
        allow.Signal(); FCHECK(FFinish(receive) == InputDeliveryResult::Delivered && FFinish(close) && FFinish(reopen));
        FCHECK(!closed_too_early && !opened_too_early && t->IsInputChannelOpen(FL(2))); t->shutdown();
    });
    cases.emplace_back("FCD-10b-open-shutdown-rollback-before-false", [] {
        auto t = FM(); FCHECK(t->init()); FacadeReceiver receiver; FacadeEvent opened, allow;
        DSoftBusTransportTestAccess::Hooks(*t, [&] { opened.Signal(); allow.Wait(); });
        auto open = std::async(std::launch::async, [&] { return t->OpenInputChannel(FL(2), &receiver, 64); });
        opened.Wait(); FCHECK(!t->IsInputChannelOpen(FL(2)));
        const auto uncommitted = DSoftBusTransportTestAccess::Capture(*t, FL(2)); const std::uint8_t byte = 1;
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, uncommitted, FL(10), &byte, 1) == InputDeliveryResult::InvalidTicket);
        t->shutdown(); allow.Signal(); FCHECK(!FFinish(open));
        const auto usage = DSoftBusTransportTestAccess::Usage(*t);
        FCHECK(usage.disabled && usage.drained && usage.open_channels == 0 && receiver.calls == 0);
    });
    cases.emplace_back("FCD-11-every-shutdown-waits-for-callback", [] {
        auto t = FM(); FCHECK(t->init()); FacadeReceiver receiver; FacadeEvent entered, allow, stopping;
        receiver.callback = [&] { entered.Signal(); allow.Wait(); };
        DSoftBusTransportTestAccess::Hooks(*t, {}, {}, [&] { stopping.Signal(); });
        FCHECK(t->OpenInputChannel(FL(2), &receiver, 64)); const auto ticket = DSoftBusTransportTestAccess::Capture(*t, FL(2));
        const std::uint8_t byte = 1;
        auto data = std::async(std::launch::async, [&] { return DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(10), &byte, 1); });
        entered.Wait();
        auto a = std::async(std::launch::async, [&] { t->shutdown(); return true; });
        auto b = std::async(std::launch::async, [&] { t->shutdown(); return true; }); stopping.Wait(2);
        const bool early_a = a.wait_for(0ms) == std::future_status::ready, early_b = b.wait_for(0ms) == std::future_status::ready;
        FCHECK(!t->IsInputChannelOpen(FL(2)) && !t->OpenInputChannel(FL(2), &receiver, 64));
        FCHECK(DSoftBusTransportTestAccess::Deliver(*t, ticket, FL(10), &byte, 1) == InputDeliveryResult::Disabled);
        allow.Signal(); FCHECK(FFinish(data) == InputDeliveryResult::Delivered && FFinish(a) && FFinish(b));
        FCHECK(!early_a && !early_b && t->state() == FacadeState::Closed);
    });
    cases.emplace_back("FCD-11b-shutdown-closes-unpublished-send-copy", [] {
        auto d = FD(); auto t = FM(d); FCHECK(t->init()); auto runtime = DSoftBusTransportTestAccess::Runtime(*t);
        FacadeEvent copying, allow;
        RuntimeSendPathTestAccess::ReplaceQueue(*runtime, SendQueueTestAccess::Make(d.send_limits, [&] { copying.Signal(); allow.Wait(); }));
        rtps::SendResourceList list; FCHECK(t->OpenOutputChannel(list, FL(10)));
        auto send = std::async(std::launch::async, [&] { return FSend(*list.front(), {FL(10), FL(11)}); }); copying.Wait();
        auto a = std::async(std::launch::async, [&] { t->shutdown(); return true; });
        auto b = std::async(std::launch::async, [&] { t->shutdown(); return true; });
        FCHECK(FFinish(a) && FFinish(b)); allow.Signal(); FCHECK(!FFinish(send));
        FCHECK(runtime->SnapshotUsage().closed && runtime->SnapshotUsage().records == 0 && !runtime->TryTake());
    });
    cases.emplace_back("FCD-12-aggregate-sender-and-close-output-noop", [] {
        auto t = FM(); FCHECK(t->init()); rtps::SendResourceList list;
        FCHECK(t->OpenOutputChannel(list, FL(2)) && t->OpenOutputChannel(list, FL(10)) && list.size() == 1);
        auto* original = list.front().get(); auto runtime = DSoftBusTransportTestAccess::Runtime(*t);
        rtps::LocatorSelectorEntry entry(3, 1); entry.unicast.push_back(FL(11)); entry.state.unicast.push_back(0);
        FCHECK(t->OpenOutputChannels(list, entry) && list.size() == 1 && list.front().get() == original);
        t->CloseOutputChannels(list, entry); FCHECK(FSend(*original, {FL(10), FL(11)}, -7));
        auto first = runtime->TryTake(), second = runtime->TryTake(); FCHECK(first && second && first->priority() == -7 && second->priority() == -7);
        FCHECK(FSend(*original, {FL(11)}, 99, true)); FCHECK(runtime->TryTake()->priority() == 0);
        rtps::SendResourceList rejected; entry.state.unicast.push_back(9);
        FCHECK(!t->OpenOutputChannels(rejected, entry) && rejected.empty()); entry.state.unicast.clear(); entry.state.unicast.push_back(0);
        auto bad = FL(10); bad.port = 0; entry.unicast.push_back(bad); entry.state.unicast.push_back(1);
        FCHECK(!t->OpenOutputChannels(rejected, entry) && rejected.empty()); entry.state.unicast.clear(); entry.state.unicast.push_back(0);
        entry.multicast.push_back(FL(10)); entry.state.multicast.push_back(0);
        FCHECK(!t->OpenOutputChannels(rejected, entry) && rejected.empty());
        auto* sender = dynamic_cast<DSoftBusSenderResource*>(original); FCHECK(sender && sender->Release());
        FCHECK(!t->OpenOutputChannel(list, FL(11)) && t->last_error().error == FacadeError::SenderAttachRejected);
    });
    cases.emplace_back("FCD-13-held-record-and-senders-outlive-facade", [] {
        rtps::SendResourceList a, b; std::shared_ptr<RuntimeSendPath> runtime; std::optional<OwnedSendRecord> held;
        {
            auto t = FM(); FCHECK(t->init()); runtime = DSoftBusTransportTestAccess::Runtime(*t);
            FCHECK(t->OpenOutputChannel(a, FL(10)) && t->OpenOutputChannel(b, FL(11)));
            a.clear(); FCHECK(FSend(*b.front(), {FL(11)})); held = runtime->TryTake(); FCHECK(held);
            t->shutdown(); t->shutdown(); FCHECK(!FSend(*b.front(), {FL(11)}));
        }
        FCHECK(held && held->data()[0] == 42 && runtime->SnapshotUsage().records == 1);
        FCHECK(!FSend(*b.front(), {FL(11)})); auto* sender = dynamic_cast<DSoftBusSenderResource*>(b.front().get());
        FCHECK(sender && sender->last_report()->status == RuntimeSendStatus::RuntimeClosed);
        held.reset(); FCHECK(runtime->SnapshotUsage().records == 0);
        auto no_init = FM(); no_init.reset();
    });
    cases.emplace_back("FCD-14-real-library-and-durable-native-failure", [] {
        FILE* capture = std::tmpfile(); FCHECK(capture); std::fflush(stderr);
        const int saved = F_DUP(F_FILENO(stderr)); FCHECK(saved >= 0 && F_DUP2(F_FILENO(capture), F_FILENO(stderr)) >= 0);
        bool failed = false;
        { auto d = FD(); std::unique_ptr<rtps::TransportInterface> t(d.create_transport()); failed = t && !t->init(); }
        std::fflush(stderr); F_DUP2(saved, F_FILENO(stderr)); F_CLOSE(saved);
        std::rewind(capture); char buffer[512]{}; const auto n = std::fread(buffer, 1, sizeof(buffer) - 1, capture); std::fclose(capture);
        FCHECK(failed && n > 0 && std::string(buffer).find("NativeBackendUnavailable") != std::string::npos);
        Locator ip; rtps::IPLocator::setIPv4(ip, 127, 0, 0, 1); FCHECK(rtps::IPLocator::toIPv4string(ip) == "127.0.0.1");
#ifdef _WIN32
        wchar_t module_path[32768]{}; auto module = GetModuleHandleW(L"fastdds-3.6.dll");
        const auto length = GetModuleFileNameW(module, module_path, 32768); FCHECK(module && length && length < 32768);
        std::wcout << L"FACADE_LOADED_FASTDDS=" << module_path << std::endl;
#endif
    });
    std::size_t failed = 0, ran = 0;
    for (const auto& item : cases) {
        if (argc == 2 && item.first != argv[1]) continue;
        ++ran;
        try { item.second(); std::cout << "PASS " << item.first << std::endl; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << item.first << ": " << error.what() << std::endl; }
    }
    std::cout << "Transport facade groups=" << ran << " failures=" << failed << std::endl;
    return ran && !failed ? 0 : 1;
}
int main(int argc, char** argv) {
    std::set_terminate([] { std::fputs("FAIL terminate\n", stderr); std::_Exit(86); });
    return RunTransportFacadeTests(argc, argv);
}
