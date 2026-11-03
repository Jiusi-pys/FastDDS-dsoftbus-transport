#include <fastdds_dsoftbus/dsoftbus/SendQueue.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Test-executable-only, calling-thread-only real allocation fault injection.
// Each armed operation fails exactly one allocation; other threads are normal.
thread_local bool queue_allocator_armed = false;
thread_local std::size_t queue_allocation_index = 0;
thread_local std::size_t queue_fail_at = (std::numeric_limits<std::size_t>::max)();
thread_local std::size_t queue_allocation_failures = 0;
void* operator new(std::size_t size)
{
    if (queue_allocator_armed && queue_allocation_index++ == queue_fail_at) {
        ++queue_allocation_failures;
        throw std::bad_alloc();
    }
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace fastdds_dsoftbus::dsoftbus;
using namespace std::chrono_literals;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace fastdds_dsoftbus::dsoftbus {
// The production constructor has no injection argument. This test-only friend
// supplies synchronization/faults without replacing any SDK ABI.
class SendQueueTestAccess
{
public:
    static std::unique_ptr<SendQueue> Make(SendQueueLimits limits,
            std::function<SendQueue::Clock::time_point()> now = {},
            std::function<void()> before = {}, std::function<void()> after = {},
            std::function<void()> wait = {})
    {
        return std::unique_ptr<SendQueue>(new SendQueue(limits,
                {std::move(now), std::move(wait), std::move(before), std::move(after)}));
    }
    static std::weak_ptr<void> State(const SendQueue& queue) { return queue.state_; }
};
}

namespace {
class QueueAllocationScope
{
public:
    explicit QueueAllocationScope(std::size_t fail_at = (std::numeric_limits<std::size_t>::max)())
    {
        queue_allocation_index = 0; queue_allocation_failures = 0;
        queue_fail_at = fail_at; queue_allocator_armed = true;
    }
    ~QueueAllocationScope() { queue_allocator_armed = false; }
};
class QueueTestEvent
{
public:
    void Open() { std::lock_guard<std::mutex> lock(mutex_); open_ = true; changed_.notify_all(); }
    bool Wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, 1s, [&] { return open_; });
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool open_ = false;
};

Locator QueueTestLocator(std::uint8_t endpoint)
{
    Locator locator(LocatorCodec::kind, LocatorCodec::port);
    std::fill(std::begin(locator.address), std::end(locator.address), std::uint8_t{0});
    locator.address[15] = endpoint;
    return locator;
}

Route QueueTestRoute(std::uint8_t remote = 3)
{
    Route route;
    route.key = {QueueTestLocator(2), QueueTestLocator(remote)};
    route.fields = {"network-b", "b.listen", "a.dial.b", {"b.dial.a"}, RecordDataType::Bytes};
    return route;
}

SendQueueLimits QueueTestLimits()
{
    return {2, 8, 4, 16, 8};
}

SendAdmission QueueTestPut(SendQueue& queue, const Route& route = QueueTestRoute(),
        std::uint64_t generation = 1)
{
    const std::uint8_t byte = 42;
    const SendBufferView view{&byte, 1};
    return queue.Enqueue(route, generation, 0, SendQueue::Clock::now() + 1s, &view, 1, 1);
}
}

int RunSendQueueTests(int argc, char** argv)
{
    static_assert(!std::is_copy_constructible_v<OwnedSendRecord>);
    static_assert(!std::is_copy_assignable_v<OwnedSendRecord>);
    static_assert(std::is_nothrow_move_constructible_v<OwnedSendRecord>);
    static_assert(std::is_nothrow_move_assignable_v<OwnedSendRecord>);
    static_assert(!std::is_move_constructible_v<SendQueue>);
    std::vector<std::pair<std::string, std::function<void()>>> cases;

    cases.emplace_back("SQ-01-prefix-and-owned-route", [] {
        SendQueue queue(QueueTestLimits());
        auto route = QueueTestRoute();
        const auto deadline = SendQueue::Clock::now() + 1s;
        {
            std::vector<std::uint8_t> a{1, 2, 3}, b{4, 5, 6};
            SendBufferView views[]{{nullptr, 0}, {a.data(), a.size()}, {b.data(), b.size()}, {nullptr, 900}};
            CHECK(queue.Enqueue(route, 17, -9, deadline, views, 4, 5) == SendAdmission::Accepted);
            std::fill(a.begin(), a.end(), std::uint8_t{99}); std::fill(b.begin(), b.end(), std::uint8_t{99});
            route.fields.remote_network_id = "mutated";
            route.fields.expected_remote_initiator_names[0] = "mutated";
            route.key.remote_locator = QueueTestLocator(9); route.route_version = 99;
        }
        auto record = queue.TryTake();
        CHECK(record && record->size() == 5);
        const std::uint8_t expected[]{1, 2, 3, 4, 5};
        CHECK(std::equal(record->data(), record->data() + record->size(), expected));
        CHECK(record->route().fields.remote_network_id == "network-b");
        CHECK(record->route().fields.expected_remote_initiator_names[0] == "b.dial.a");
        CHECK(record->route().key.remote_locator == QueueTestLocator(3));
        CHECK(record->route().route_version == 1 && record->generation() == 17);
        CHECK(record->priority() == -9 && record->deadline() == deadline);
        CHECK(queue.SnapshotUsage().records == 1 && queue.SnapshotUsage().bytes == 5);
        record.reset();
        CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-02-invalid-inputs-and-route", [] {
        SendQueue queue(QueueTestLimits());
        const std::uint8_t byte = 1;
        const SendBufferView valid{&byte, 1}, invalid{nullptr, 1};
        const auto deadline = SendQueue::Clock::now() + 1s;
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, deadline, nullptr, 1, 1) == SendAdmission::InvalidBuffers);
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, deadline, &invalid, 1, 1) == SendAdmission::InvalidBuffers);
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, deadline, &valid, 1, 2) == SendAdmission::InvalidBuffers);
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, deadline, &valid, 1, 0) == SendAdmission::InvalidBuffers);
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, deadline, &valid, 1, 9) == SendAdmission::TooLarge);
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, SendQueue::Clock::now(), &valid, 1, 1) == SendAdmission::DeadlineExpired);
        CHECK(QueueTestPut(queue, QueueTestRoute(), 0) == SendAdmission::InvalidRoute);
        for (int i = 0; i < 9; ++i) {
            auto bad = QueueTestRoute();
            if (i == 0) bad.route_version = 0;
            if (i == 1) bad.key.remote_locator.port = 0;
            if (i == 2) bad.key.local_locator.kind = 1;
            if (i == 3) bad.fields.data_type = RecordDataType::Unsupported;
            if (i == 4) bad.fields.remote_network_id.clear();
            if (i == 5) bad.fields.remote_listen_name.assign(256, 'a');
            if (i == 6) bad.fields.local_initiator_name = std::string("a\0b", 3);
            if (i == 7) bad.fields.expected_remote_initiator_names.clear();
            if (i == 8) bad.fields.expected_remote_initiator_names.push_back("b.dial.a");
            CHECK(QueueTestPut(queue, bad) == SendAdmission::InvalidRoute);
        }
        CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-03-zero-configuration", [] {
        for (int field = 0; field < 5; ++field) {
            auto limits = QueueTestLimits();
            if (field == 0) limits.peer_records = 0;
            if (field == 1) limits.peer_bytes = 0;
            if (field == 2) limits.global_records = 0;
            if (field == 3) limits.global_bytes = 0;
            if (field == 4) limits.max_record_bytes = 0;
            bool rejected = false;
            try { SendQueue queue(limits); } catch (const std::invalid_argument&) { rejected = true; }
            CHECK(rejected);
        }
    });
    cases.emplace_back("SQ-04-stable-peer-and-inflight-count", [] {
        auto limits = QueueTestLimits(); limits.peer_records = 1;
        SendQueue queue(limits);
        CHECK(QueueTestPut(queue) == SendAdmission::Accepted);
        auto record = queue.TryTake();
        auto changed = QueueTestRoute(); changed.route_version = 88;
        changed.fields.remote_network_id = "new-network";
        changed.key.local_locator = QueueTestLocator(8);
        CHECK(QueueTestPut(queue, changed, 99) == SendAdmission::DeadlineExpired);
        CHECK(queue.SnapshotUsage().records == 1 && queue.SnapshotUsage().peers == 1);
        CHECK(QueueTestPut(queue, QueueTestRoute(4)) == SendAdmission::Accepted);
        record.reset();
        CHECK(QueueTestPut(queue, changed, 99) == SendAdmission::Accepted);
    });
    cases.emplace_back("SQ-05-peer-and-global-byte-limits", [] {
        SendQueue queue({8, 5, 8, 7, 8});
        const std::uint8_t data[]{1, 2, 3, 4, 5, 6, 7, 8};
        const SendBufferView buffer{data, 8};
        const auto put = [&](std::uint8_t peer, std::size_t bytes) {
            return queue.Enqueue(QueueTestRoute(peer), 1, 0, SendQueue::Clock::now() + 1s, &buffer, 1, bytes);
        };
        CHECK(put(3, 4) == SendAdmission::Accepted);
        CHECK(put(3, 2) == SendAdmission::DeadlineExpired);
        CHECK(put(4, 3) == SendAdmission::Accepted);
        CHECK(put(5, 1) == SendAdmission::DeadlineExpired);
        CHECK(queue.SnapshotUsage().records == 2 && queue.SnapshotUsage().bytes == 7);
        CHECK(queue.SnapshotUsage().peers == 2);
        queue.Close();
        CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().bytes == 0);
    });
    cases.emplace_back("SQ-06-global-record-limit-and-no-empty-peers", [] {
        SendQueue queue({8, 8, 1, 8, 8});
        CHECK(QueueTestPut(queue) == SendAdmission::Accepted);
        for (std::uint8_t peer = 4; peer < 14; ++peer) {
            CHECK(QueueTestPut(queue, QueueTestRoute(peer)) == SendAdmission::DeadlineExpired);
            CHECK(queue.SnapshotUsage().peers == 1);
        }
        queue.Close();
        CHECK(queue.SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-07-safe-capacity-arithmetic", [] {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        SendQueue queue({maximum, maximum, maximum, maximum, maximum});
        CHECK(QueueTestPut(queue) == SendAdmission::Accepted);
        const std::uint8_t byte = 1;
        // Prefix advertises maximum bytes, but cannot fit alongside one byte.
        // A correct capacity check never allocates or reads this advertised data.
        const SendBufferView huge{&byte, maximum};
        CHECK(queue.Enqueue(QueueTestRoute(), 1, 0, SendQueue::Clock::now() + 2ms,
                &huge, 1, maximum) == SendAdmission::DeadlineExpired);
        CHECK(queue.SnapshotUsage().bytes == 1 && queue.SnapshotUsage().records == 1);
    });
    cases.emplace_back("SQ-08-copy-exceptions-refund", [] {
        for (bool after : {false, true}) {
            auto fail = [] { throw std::bad_alloc(); };
            auto queue = SendQueueTestAccess::Make(QueueTestLimits(), {}, after ? std::function<void()>{} : fail,
                    after ? fail : std::function<void()>{});
            CHECK(QueueTestPut(*queue) == SendAdmission::CopyFailed);
            CHECK(queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().bytes == 0);
            CHECK(queue->SnapshotUsage().peers == 0 && !queue->TryTake());
        }
    });
    cases.emplace_back("SQ-09-reservation-close-copy-race", [] {
        QueueTestEvent copying, release;
        auto queue = SendQueueTestAccess::Make(QueueTestLimits(), {}, [&] {
            copying.Open(); if (!release.Wait()) throw std::runtime_error("copy release timeout");
        });
        auto operation = std::async(std::launch::async, [&] { return QueueTestPut(*queue); });
        const bool entered = copying.Wait();
        const auto reserved = queue->SnapshotUsage();
        queue->Close(); release.Open();
        CHECK(operation.get() == SendAdmission::Closed);
        CHECK(entered && reserved.records == 1 && reserved.bytes == 1 && reserved.queued == 0);
        CHECK(!queue->TryTake() && queue->SnapshotUsage().records == 0);
    });
    cases.emplace_back("SQ-10-publish-order-not-reservation-order", [] {
        QueueTestEvent first_copy, release;
        std::atomic<unsigned> calls{0};
        const auto fixed_now = SendQueue::Clock::now();
        auto queue = SendQueueTestAccess::Make(QueueTestLimits(), [fixed_now] { return fixed_now; }, [&] {
            if (calls.fetch_add(1) == 0) {
                first_copy.Open(); if (!release.Wait()) throw std::runtime_error("copy release timeout");
            }
        });
        auto first = std::async(std::launch::async, [&] { return QueueTestPut(*queue, QueueTestRoute(), 11); });
        const bool entered = first_copy.Wait();
        const auto second = QueueTestPut(*queue, QueueTestRoute(), 22);
        release.Open(); const auto first_result = first.get();
        CHECK(entered && second == SendAdmission::Accepted && first_result == SendAdmission::Accepted);
        auto one = queue->TryTake(); auto two = queue->TryTake();
        CHECK(one && two && one->generation() == 22 && two->generation() == 11);
    });
    cases.emplace_back("SQ-11-expire-queued-records", [] {
        const auto base = SendQueue::Clock::now() + 1h;
        std::atomic<int> offset{0};
        auto queue = SendQueueTestAccess::Make(QueueTestLimits(), [&] { return base + std::chrono::milliseconds(offset.load()); });
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        CHECK(queue->Enqueue(QueueTestRoute(), 1, 0, base + 5ms, &view, 1, 1) == SendAdmission::Accepted);
        CHECK(queue->Enqueue(QueueTestRoute(), 2, 0, base + 50ms, &view, 1, 1) == SendAdmission::Accepted);
        offset = 5;
        auto record = queue->TryTake();
        CHECK(record && record->generation() == 2);
        CHECK(queue->SnapshotUsage().records == 1 && queue->SnapshotUsage().bytes == 1);
        CHECK(!queue->TryTake()); record.reset();
        CHECK(queue->SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-12-one-admission-deadline-through-wait-and-copy", [] {
        const auto base = SendQueue::Clock::now() + 1h;
        std::atomic<int> offset{0}; std::atomic<unsigned> copies{0};
        QueueTestEvent waiting;
        auto limits = QueueTestLimits(); limits.peer_records = 1;
        auto queue = SendQueueTestAccess::Make(limits,
                [&] { return base + std::chrono::milliseconds(offset.load()); },
                [&] { if (copies.fetch_add(1) == 1) offset = 21; }, {}, [&] { waiting.Open(); });
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        CHECK(queue->Enqueue(QueueTestRoute(), 1, 0, base + 100ms, &view, 1, 1) == SendAdmission::Accepted);
        auto held = queue->TryTake();
        auto later = std::async(std::launch::async, [&] {
            return queue->Enqueue(QueueTestRoute(), 2, 0, base + 100ms, &view, 1, 1);
        });
        const bool did_wait = waiting.Wait();
        offset = 15; held.reset();
        const auto result = later.get();
        CHECK(did_wait && result == SendAdmission::DeadlineExpired);
        CHECK(queue->SnapshotUsage().records == 0 && !queue->TryTake());
    });
    cases.emplace_back("SQ-13-copy-obeys-original-deadline-and-equality", [] {
        const auto base = SendQueue::Clock::now() + 1h;
        std::atomic<int> offset{0};
        auto queue = SendQueueTestAccess::Make(QueueTestLimits(),
                [&] { return base + std::chrono::milliseconds(offset.load()); }, {}, [&] { offset = 5; });
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        CHECK(queue->Enqueue(QueueTestRoute(), 1, 0, base + 5ms, &view, 1, 1) == SendAdmission::DeadlineExpired);
        CHECK(queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-14-taken-record-survives-close-and-queue", [] {
        std::optional<OwnedSendRecord> held;
        {
            auto queue = std::make_unique<SendQueue>(QueueTestLimits());
            CHECK(QueueTestPut(*queue) == SendAdmission::Accepted);
            CHECK(QueueTestPut(*queue) == SendAdmission::Accepted);
            held = queue->TryTake(); queue->Close(); queue->Close();
            CHECK(queue->SnapshotUsage().closed && queue->SnapshotUsage().queued == 0);
            CHECK(queue->SnapshotUsage().records == 1 && queue->SnapshotUsage().bytes == 1);
            CHECK(QueueTestPut(*queue) == SendAdmission::Closed);
            CHECK(!queue->TryTake());
        }
        CHECK(held && held->size() == 1 && held->data()[0] == 42);
        CHECK(held->route().fields.remote_network_id == "network-b");
        auto moved = std::move(*held); held.reset();
        CHECK(moved.data()[0] == 42);
    });
    cases.emplace_back("SQ-15-close-wakes-waiter", [] {
        QueueTestEvent waiting;
        auto queue = SendQueueTestAccess::Make({1, 8, 1, 8, 8}, {}, {}, {}, [&] { waiting.Open(); });
        CHECK(QueueTestPut(*queue) == SendAdmission::Accepted);
        auto operation = std::async(std::launch::async, [&] { return QueueTestPut(*queue); });
        const bool entered = waiting.Wait(); queue->Close();
        CHECK(operation.get() == SendAdmission::Closed);
        CHECK(entered && queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
    });
    cases.emplace_back("SQ-16-move-assignment-releases-previous-lease", [] {
        SendQueue queue(QueueTestLimits());
        CHECK(QueueTestPut(queue) == SendAdmission::Accepted);
        CHECK(QueueTestPut(queue, QueueTestRoute(4), 2) == SendAdmission::Accepted);
        auto first = queue.TryTake(), second = queue.TryTake();
        CHECK(first && second && queue.SnapshotUsage().records == 2);
        *first = std::move(*second); second.reset();
        CHECK(queue.SnapshotUsage().records == 1 && queue.SnapshotUsage().peers == 1);
        CHECK(first->generation() == 2); first.reset();
        CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().bytes == 0);
    });

    cases.emplace_back("SQ-17-batch-owned-order-and-lease", [] {
        SendQueue queue(QueueTestLimits());
        const auto deadline = SendQueue::Clock::now() + 1s;
        {
            auto a = QueueTestRoute(3), b = QueueTestRoute(4);
            const SendTargetView targets[]{{&a, 11}, {&b, 22}};
            std::uint8_t bytes[]{1, 2, 3};
            const SendBufferView views[]{{bytes, 2}, {bytes + 2, 1}, {nullptr, 900}};
            CHECK(queue.EnqueueBatch(targets, 2, -7, deadline, views, 3, 3) == SendAdmission::Accepted);
            std::fill(std::begin(bytes), std::end(bytes), std::uint8_t{99});
            a.fields.remote_network_id = "changed"; b.route_version = 99;
        }
        CHECK(queue.SnapshotUsage().records == 2 && queue.SnapshotUsage().bytes == 6);
        auto first = queue.TryTake(), second = queue.TryTake();
        CHECK(first && second && !queue.TryTake());
        CHECK(first->route().key.remote_locator == QueueTestLocator(3) && first->generation() == 11);
        CHECK(second->route().key.remote_locator == QueueTestLocator(4) && second->generation() == 22);
        CHECK(first->route().fields.remote_network_id == "network-b" && second->route().route_version == 1);
        CHECK(first->priority() == -7 && second->priority() == -7);
        CHECK(first->deadline() == deadline && second->deadline() == deadline);
        CHECK(first->size() == 3 && second->size() == 3 && first->data() != second->data());
        const std::uint8_t expected[]{1, 2, 3};
        CHECK(std::equal(first->data(), first->data() + 3, expected));
        CHECK(std::equal(second->data(), second->data() + 3, expected));
        first.reset(); CHECK(queue.SnapshotUsage().records == 1 && queue.SnapshotUsage().bytes == 3);
        second.reset(); CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().peers == 0);
    });

    cases.emplace_back("SQ-18-batch-invalid-set-publishes-none", [] {
        SendQueue queue(QueueTestLimits());
        auto a = QueueTestRoute(3), b = QueueTestRoute(4);
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        const auto deadline = SendQueue::Clock::now() + 1s;
        SendTargetView targets[]{{&a, 1}, {&b, 2}};
        CHECK(queue.EnqueueBatch(nullptr, 2, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        CHECK(queue.EnqueueBatch(targets, 0, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        targets[1].route = nullptr;
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        targets[1].route = &b; targets[1].generation = 0;
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        targets[1].generation = 2; b.route_version = 0;
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        b = a; b.key.local_locator = QueueTestLocator(8); b.route_version = 2;
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &view, 1, 1) == SendAdmission::InvalidRoute);
        b = QueueTestRoute(4);
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, nullptr, 1, 1) == SendAdmission::InvalidBuffers);
        CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &view, 1, 0) == SendAdmission::InvalidBuffers);
        CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().peers == 0 && !queue.TryTake());
    });
    cases.emplace_back("SQ-19-batch-impossible-count-bytes-overflow", [] {
        auto a = QueueTestRoute(3), b = QueueTestRoute(4);
        const SendTargetView targets[]{{&a, 1}, {&b, 2}};
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        const std::uint8_t byte = 1;
        const SendBufferView huge{&byte, maximum};
        const auto deadline = SendQueue::Clock::now() + 1s;
        for (auto limits : {SendQueueLimits{8, 8, 1, 16, 8},
                SendQueueLimits{8, 8, 4, 7, 8},
                SendQueueLimits{maximum, maximum, maximum, maximum, maximum}}) {
            SendQueue queue(limits);
            const auto bytes = limits.max_record_bytes == maximum ? maximum : std::size_t{4};
            CHECK(queue.EnqueueBatch(targets, 2, 0, deadline, &huge, 1, bytes) == SendAdmission::TooLarge);
            CHECK(queue.SnapshotUsage().records == 0 && queue.SnapshotUsage().peers == 0);
        }
    });
    cases.emplace_back("SQ-20-batch-saturation-reserves-no-prefix", [] {
        for (int limit = 0; limit < 3; ++limit) {
            const auto base = SendQueue::Clock::now() + 1h;
            std::atomic<int> offset{0}; QueueTestEvent waiting;
            auto limits = QueueTestLimits();
            if (limit == 0) limits.peer_records = 1;
            if (limit == 1) limits.global_records = 2;
            if (limit == 2) limits.global_bytes = 2;
            auto queue = SendQueueTestAccess::Make(limits,
                    [&] { return base + std::chrono::milliseconds(offset.load()); }, {}, {}, [&] { waiting.Open(); });
            auto a = QueueTestRoute(3), b = QueueTestRoute(4);
            const SendTargetView targets[]{{&a, 1}, {&b, 2}};
            const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
            CHECK(queue->Enqueue(b, 3, 0, base + 1s, &view, 1, 1) == SendAdmission::Accepted);
            auto held = queue->TryTake();
            auto operation = std::async(std::launch::async, [&] {
                return queue->EnqueueBatch(targets, 2, 0, base + 1s, &view, 1, 1);
            });
            const bool waited = waiting.Wait(); const auto during = queue->SnapshotUsage();
            const bool published = queue->TryTake().has_value();
            offset = 20; held.reset();
            const auto result = operation.get();
            CHECK(waited && !published && during.records == 1 && during.bytes == 1 && during.peers == 1);
            CHECK(result == SendAdmission::DeadlineExpired);
            CHECK(queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0 && !queue->TryTake());
        }
    });
    cases.emplace_back("SQ-21-batch-every-real-allocation-failure-refunds", [] {
        std::vector<Route> routes;
        for (std::uint8_t id : {3, 4, 5}) {
            auto route = QueueTestRoute(id);
            route.fields.remote_network_id.assign(64, 'n');
            route.fields.remote_listen_name.assign(120, 'l');
            route.fields.local_initiator_name.assign(120, 'i');
            route.fields.expected_remote_initiator_names = {std::string(120, 'e'), std::string(120, 'f')};
            routes.push_back(std::move(route));
        }
        const SendTargetView targets[]{{&routes[0], 11}, {&routes[1], 22}, {&routes[2], 33}};
        const std::uint8_t bytes[]{1, 2, 3}; const SendBufferView view{bytes, 3};
        const auto now = SendQueue::Clock::now();
        std::size_t allocations = 0;
        for (int retained = 0; retained < 2; ++retained) {
            // Include an existing charged peer in the second sweep: rollback
            // must not erase a preexisting node or discard its queued record.
            allocations = 0;
            for (std::size_t fail_at = 0;; ++fail_at) {
                auto queue = SendQueueTestAccess::Make({4, 16, 8, 32, 8}, [now] { return now; });
                if (retained) CHECK(QueueTestPut(*queue, routes[1], 99) == SendAdmission::Accepted);
                SendAdmission result; std::size_t attempted, failures;
                {
                    QueueAllocationScope inject(fail_at == 0 ? (std::numeric_limits<std::size_t>::max)() : fail_at - 1);
                    result = queue->EnqueueBatch(targets, 3, 7, now + 1s, &view, 1, 3);
                    attempted = queue_allocation_index; failures = queue_allocation_failures;
                }
                if (fail_at == 0) {
                    CHECK(result == SendAdmission::Accepted && failures == 0);
                    allocations = attempted;
                    CHECK(allocations >= 1 + 3 * 7); // Scratch + real copies for all 3 targets.
                } else {
                    CHECK(result == SendAdmission::CopyFailed && failures == 1 && attempted == fail_at);
                    const auto usage = queue->SnapshotUsage();
                    CHECK(usage.records == static_cast<std::size_t>(retained));
                    CHECK(usage.bytes == static_cast<std::size_t>(retained) && usage.peers == static_cast<std::size_t>(retained));
                    if (retained) { auto old = queue->TryTake(); CHECK(old && old->generation() == 99); }
                    CHECK(!queue->TryTake());
                    CHECK(queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
                    CHECK(queue->EnqueueBatch(targets, 3, 7, now + 1s, &view, 1, 3) == SendAdmission::Accepted);
                }
                if (fail_at == allocations) break;
            }
            std::cout << "allocation sweep retained=" << retained << " failed_points=" << allocations << '\n';
        }
    });
    cases.emplace_back("SQ-22-batch-staged-prefix-close-and-expiry", [] {
        for (int action = 0; action < 3; ++action) {
            const auto base = SendQueue::Clock::now() + 1h;
            std::atomic<int> offset{0}; std::atomic<unsigned> copied{0};
            QueueTestEvent first_copied, release;
            auto queue = SendQueueTestAccess::Make(QueueTestLimits(),
                    [&] { return base + std::chrono::milliseconds(offset.load()); }, {}, [&] {
                        if (copied.fetch_add(1) == 0) {
                            first_copied.Open(); if (!release.Wait()) throw std::runtime_error("batch copy release timeout");
                        }
                    });
            auto a = QueueTestRoute(3), b = QueueTestRoute(4);
            const SendTargetView targets[]{{&a, 1}, {&b, 2}};
            const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
            auto operation = std::async(std::launch::async, [&] {
                return queue->EnqueueBatch(targets, 2, 4, base + 5ms, &view, 1, 1);
            });
            const bool entered = first_copied.Wait(); const auto during = queue->SnapshotUsage();
            const bool partial = queue->TryTake().has_value();
            if (action == 1) queue->Close();
            if (action == 2) offset = 5;
            const auto after_action = queue->SnapshotUsage();
            release.Open(); const auto result = operation.get();
            CHECK(entered && !partial && during.records == 2 && during.bytes == 2 && during.peers == 2 && during.queued == 0);
            CHECK(after_action.records == 2 && after_action.bytes == 2 && after_action.queued == 0);
            CHECK(result == (action == 0 ? SendAdmission::Accepted : action == 1 ? SendAdmission::Closed : SendAdmission::DeadlineExpired));
            if (action == 0) {
                auto first = queue->TryTake(), second = queue->TryTake();
                CHECK(first && second && first->generation() == 1 && second->generation() == 2);
            }
            CHECK(!queue->TryTake() && queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
        }
    });
    cases.emplace_back("SQ-23-batch-one-budget-across-wait-and-each-copy", [] {
        const auto base = SendQueue::Clock::now() + 1h;
        std::atomic<int> offset{0}; std::atomic<unsigned> copies{0}; QueueTestEvent waiting;
        auto queue = SendQueueTestAccess::Make({1, 8, 4, 16, 8},
                [&] { return base + std::chrono::milliseconds(offset.load()); }, {},
                [&] { if (copies.fetch_add(1) == 2) offset = 20; }, [&] { waiting.Open(); });
        auto a = QueueTestRoute(3), b = QueueTestRoute(4);
        const SendTargetView targets[]{{&a, 1}, {&b, 2}};
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        CHECK(queue->Enqueue(b, 3, 0, base + 1s, &view, 1, 1) == SendAdmission::Accepted);
        auto held = queue->TryTake();
        auto operation = std::async(std::launch::async, [&] {
            return queue->EnqueueBatch(targets, 2, 4, base + 1s, &view, 1, 1);
        });
        const bool waited = waiting.Wait(); offset = 15; held.reset();
        const auto result = operation.get();
        CHECK(waited && result == SendAdmission::DeadlineExpired && copies == 3);
        CHECK(!queue->TryTake() && queue->SnapshotUsage().records == 0);
    });
    cases.emplace_back("SQ-24-batch-inflight-leases-survive-queue", [] {
        std::optional<OwnedSendRecord> first, second; std::weak_ptr<void> state;
        {
            auto queue = std::make_unique<SendQueue>(QueueTestLimits());
            state = SendQueueTestAccess::State(*queue);
            auto a = QueueTestRoute(3), b = QueueTestRoute(4);
            const SendTargetView targets[]{{&a, 1}, {&b, 2}};
            const std::uint8_t bytes[]{8, 9}; const SendBufferView view{bytes, 2};
            CHECK(queue->EnqueueBatch(targets, 2, 9, SendQueue::Clock::now() + 1s, &view, 1, 2) == SendAdmission::Accepted);
            first = queue->TryTake(); second = queue->TryTake(); queue->Close();
            CHECK(queue->SnapshotUsage().records == 2 && queue->SnapshotUsage().bytes == 4);
            CHECK(queue->SnapshotUsage().queued == 0 && queue->SnapshotUsage().peers == 2);
        }
        CHECK(!state.expired() && first && second && first->data()[0] == 8 && second->data()[1] == 9);
        first.reset(); CHECK(!state.expired());
        second.reset(); CHECK(state.expired());
    });
    cases.emplace_back("SQ-25-batch-contiguous-publication-order", [] {
        const auto now = SendQueue::Clock::now();
        QueueTestEvent staged, release; std::atomic<unsigned> copied{0};
        auto queue = SendQueueTestAccess::Make(QueueTestLimits(), [now] { return now; }, {}, [&] {
            if (copied.fetch_add(1) == 0) {
                staged.Open(); if (!release.Wait()) throw std::runtime_error("publication release timeout");
            }
        });
        auto a = QueueTestRoute(3), b = QueueTestRoute(4);
        const SendTargetView targets[]{{&a, 11}, {&b, 22}};
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        auto operation = std::async(std::launch::async, [&] {
            return queue->EnqueueBatch(targets, 2, 4, now + 1s, &view, 1, 1);
        });
        const bool entered = staged.Wait();
        const auto other = QueueTestPut(*queue, QueueTestRoute(5), 33);
        auto earlier = queue->TryTake();
        const bool partial = queue->TryTake().has_value();
        release.Open(); const auto result = operation.get();
        CHECK(entered && other == SendAdmission::Accepted && earlier && earlier->generation() == 33 && !partial);
        CHECK(result == SendAdmission::Accepted);
        auto first = queue->TryTake(), second = queue->TryTake();
        CHECK(first && second && first->generation() == 11 && second->generation() == 22);
        CHECK(!queue->TryTake());
    });
    cases.emplace_back("SQ-26-large-batch-iterative-abort-and-close", [] {
        constexpr std::size_t count = 2048;
        std::vector<Route> routes; routes.reserve(count);
        std::vector<SendTargetView> targets; targets.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto route = QueueTestRoute(3);
            route.key.remote_locator.address[13] = 1;
            route.key.remote_locator.address[14] = static_cast<std::uint8_t>(i >> 8);
            route.key.remote_locator.address[15] = static_cast<std::uint8_t>(i);
            routes.push_back(std::move(route));
            targets.push_back({&routes.back(), i + 1});
        }
        const auto now = SendQueue::Clock::now();
        const std::uint8_t byte = 1; const SendBufferView view{&byte, 1};
        std::size_t copies = 0;
        auto queue = SendQueueTestAccess::Make({1, 1, count, count, 1}, [now] { return now; }, {}, [&] {
            if (++copies == count) throw std::bad_alloc();
        });
        CHECK(queue->EnqueueBatch(targets.data(), count, 1, now + 1s, &view, 1, 1) == SendAdmission::CopyFailed);
        CHECK(!queue->TryTake() && queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
        CHECK(queue->EnqueueBatch(targets.data(), count, 1, now + 1s, &view, 1, 1) == SendAdmission::Accepted);
        CHECK(queue->SnapshotUsage().queued == count && queue->SnapshotUsage().bytes == count);
        queue->Close();
        CHECK(queue->SnapshotUsage().records == 0 && queue->SnapshotUsage().peers == 0);
    });

    std::size_t failures = 0, executed = 0;
    for (const auto& test : cases) {
        if (argc == 2 && test.first != argv[1]) continue;
        ++executed;
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << "SendQueue groups=" << executed << " failures=" << failures << '\n';
    return executed != 0 && failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) { return RunSendQueueTests(argc, argv); }
