#include <mdds/dsoftbus/InputChannels.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
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

// Test-executable-only allocator injection. Arm only on the calling thread;
// concurrent worker/test allocations stay normal. The ordinal sweep additionally
// covers construction, including MSVC Debug iterator-proxy allocations.
thread_local bool fail_next_input_allocation = false;
thread_local unsigned input_allocation_failures = 0;
thread_local std::ptrdiff_t input_fail_after = -1;
thread_local bool count_input_allocations = false;
thread_local std::size_t input_allocations_observed = 0;
void* operator new(std::size_t size)
{
    if (count_input_allocations) ++input_allocations_observed;
    if (fail_next_input_allocation || input_fail_after == 0) {
        fail_next_input_allocation = false;
        input_fail_after = -1;
        ++input_allocation_failures;
        throw std::bad_alloc();
    }
    if (input_fail_after > 0) --input_fail_after;
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace mdds::dsoftbus;
using namespace std::chrono_literals;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace mdds::dsoftbus {
class InputChannelsTestAccess
{
public:
    static std::unique_ptr<InputChannels> Make(EndpointDirectory::Snapshot directory,
            std::function<void()> publish = {},
            std::function<void(const Locator&)> close = {}, std::function<void()> disable = {})
    {
        return std::unique_ptr<InputChannels>(new InputChannels(std::move(directory), 8,
                {std::move(publish), std::move(close), std::move(disable)}));
    }
    static std::weak_ptr<void> Entry(const InputChannelTicket& ticket) { return ticket.entry_; }
    static bool SameEntry(const InputChannelTicket& a, const InputChannelTicket& b)
    { return a.entry_ && a.entry_ == b.entry_; }
    static bool SameIdentity(const InputChannelTicket& a, const InputChannelTicket& b)
    { return a.identity_ && a.identity_ == b.identity_; }
};
}

namespace {
class InputTestEvent
{
public:
    void Signal() { std::lock_guard<std::mutex> lock(mutex_); ++count_; changed_.notify_all(); }
    void Wait(std::size_t count = 1)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // Watchdog failure is not an ordering oracle or a normal cleanup path.
        if (!changed_.wait_for(lock, 3s, [&] { return count_ >= count; })) {
            std::cerr << "FAIL synchronization watchdog" << std::endl;
            std::abort();
        }
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::size_t count_ = 0;
};

template<class T> T Finish(std::future<T>& future)
{
    if (future.wait_for(3s) != std::future_status::ready) {
        std::cerr << "FAIL completion watchdog" << std::endl;
        std::abort();
    }
    return future.get();
}

Locator InputTestLocator(std::uint8_t id)
{
    EndpointId endpoint{};
    endpoint[15] = id;
    Locator result;
    CHECK(LocatorCodec::Encode(endpoint, result));
    return result;
}

EndpointDirectory::Snapshot InputTestDirectory()
{
    DirectoryConfiguration config;
    for (std::uint8_t local : {std::uint8_t{2}, std::uint8_t{3}}) {
        const auto suffix = std::to_string(local);
        config.locals.push_back({InputTestLocator(local), "namespace" + suffix, "listen" + suffix});
        Route route;
        route.key = {InputTestLocator(local), InputTestLocator(4)};
        route.fields = {"remote-network", "remote-listen", "dial" + suffix,
                {"remote-dial" + suffix}, RecordDataType::Bytes};
        config.routes.push_back(route);
    }
    auto result = EndpointDirectory::Build(config);
    CHECK(result.value);
    return *result.value;
}

class InputTestReceiver : public InputChannels::Receiver
{
public:
    std::atomic<std::size_t> calls{0};
    std::function<void(const std::uint8_t*, std::uint32_t, const Locator&, const Locator&)> receive;
    void OnDataReceived(const std::uint8_t* data, const std::uint32_t size,
            const Locator& local, const Locator& remote) override
    {
        ++calls;
        if (receive) receive(data, size, local, remote);
    }
};

InputDeliveryResult InputTestDeliver(InputChannels& core, const InputChannelTicket& ticket)
{
    const std::uint8_t bytes[]{0x52, 0, 0x54, 0x50};
    return core.Deliver(ticket, InputTestLocator(4), bytes, sizeof(bytes));
}
}

int RunInputChannelsTests(int argc, char** argv)
{
    static_assert(!std::is_move_constructible_v<InputChannels>);
    static_assert(!std::is_copy_constructible_v<InputChannels>);
    static_assert(std::is_copy_constructible_v<InputChannelTicket>);
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    cases.emplace_back("IC-01-construction-and-invalid-open", [] {
        const auto directory = InputTestDirectory();
        for (int invalid = 0; invalid < 3; ++invalid) {
            if (invalid == 2 && sizeof(std::size_t) <= sizeof(std::uint32_t)) continue;
            bool rejected = false;
            try {
                InputChannels core(invalid == 0 ? nullptr : directory, invalid == 1 ? 0 :
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1);
            } catch (const std::invalid_argument&) { rejected = true; }
            CHECK(rejected);
        }
        InputTestReceiver receiver;
        InputChannels maximum(directory, std::numeric_limits<std::uint32_t>::max());
        InputChannels core(directory, 1);
        CHECK(core.Open(InputTestLocator(2), nullptr) == InputOpenResult::NullReceiver);
        CHECK(core.Open(InputTestLocator(8), &receiver) == InputOpenResult::InvalidLocal);
        auto malformed = InputTestLocator(2); malformed.port = 0;
        CHECK(core.Open(malformed, &receiver) == InputOpenResult::InvalidLocal);
        CHECK(core.Open(Locator{}, &receiver) == InputOpenResult::InvalidLocal);
        CHECK(core.SnapshotUsage().open_channels == 0);
    });
    cases.emplace_back("IC-02-idempotence-conflicts-and-allocation", [] {
        bool fail = false;
        InputTestReceiver a, b;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), [&] {
            if (fail) throw std::bad_alloc();
        });
        CHECK(core->Open(InputTestLocator(2), &a) == InputOpenResult::Opened);
        auto ticket = core->Capture(InputTestLocator(2));
        CHECK(core->Open(InputTestLocator(2), &a) == InputOpenResult::AlreadyOpen);
        CHECK(InputChannelsTestAccess::SameEntry(ticket, core->Capture(InputTestLocator(2))));
        CHECK(core->Open(InputTestLocator(2), &b) == InputOpenResult::Conflict);
        CHECK(core->Open(InputTestLocator(3), &a) == InputOpenResult::Conflict);
        fail = true;
        CHECK(core->Open(InputTestLocator(3), &b) == InputOpenResult::AllocationFailure);
        CHECK(core->SnapshotUsage().open_channels == 1);
        CHECK(InputTestDeliver(*core, ticket) == InputDeliveryResult::Delivered);
        fail = false;
        CHECK(core->Open(InputTestLocator(3), &b) == InputOpenResult::Opened);
        core->Close(InputTestLocator(2));
        core->Close(InputTestLocator(2));
        CHECK(InputTestDeliver(*core, ticket) == InputDeliveryResult::Closed);
        CHECK(core->SnapshotUsage().open_channels == 1);
    });
    cases.emplace_back("IC-03-real-interface-borrowed-bytes-and-lock-release", [] {
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8);
        const std::uint8_t bytes[]{0, 2, 3, 255, 5, 6, 7, 8};
        receiver.receive = [&](const std::uint8_t* data, std::uint32_t size,
                const Locator& local, const Locator& remote) {
            CHECK(data == bytes && size == sizeof(bytes));
            CHECK(std::equal(data, data + size, bytes));
            CHECK(local == InputTestLocator(2) && remote == InputTestLocator(4));
            // Reentry into inspection/admission is safe because no core lock is held.
            CHECK(core.SnapshotUsage().in_flight == 1);
            CHECK(core.Open(local, &receiver) == InputOpenResult::AlreadyOpen);
        };
        CHECK(core.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        const auto ticket = core.Capture(InputTestLocator(2));
        CHECK(core.Deliver(ticket, InputTestLocator(4), bytes, sizeof(bytes)) == InputDeliveryResult::Delivered);
        CHECK(receiver.calls == 1 && core.SnapshotUsage().in_flight == 0);
    });
    cases.emplace_back("IC-04-route-and-length-rejection", [] {
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8);
        CHECK(core.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        const auto ticket = core.Capture(InputTestLocator(2));
        const std::uint8_t byte = 9;
        CHECK(core.Deliver(ticket, InputTestLocator(4), nullptr, 1) == InputDeliveryResult::InvalidBuffer);
        CHECK(core.Deliver(ticket, InputTestLocator(4), &byte, 0) == InputDeliveryResult::InvalidBuffer);
        CHECK(core.Deliver(ticket, InputTestLocator(4), &byte, 9) == InputDeliveryResult::TooLarge);
        if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
            InputChannels wide(InputTestDirectory(), std::numeric_limits<std::uint32_t>::max());
            CHECK(wide.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
            CHECK(wide.Deliver(wide.Capture(InputTestLocator(2)), InputTestLocator(4), &byte,
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1)
                    == InputDeliveryResult::TooLarge);
        }
        CHECK(core.Deliver(ticket, InputTestLocator(8), &byte, 1) == InputDeliveryResult::InvalidRoute);
        CHECK(core.Deliver(ticket, InputTestLocator(3), &byte, 1) == InputDeliveryResult::InvalidRoute);
        auto bad = InputTestLocator(4); bad.kind = 0;
        CHECK(core.Deliver(ticket, bad, &byte, 1) == InputDeliveryResult::InvalidRoute);
        CHECK(receiver.calls == 0 && core.SnapshotUsage().in_flight == 0);
    });
    cases.emplace_back("IC-05-ticket-incarnations-and-bounded-history", [] {
        InputTestReceiver old_receiver, new_receiver;
        InputChannels core(InputTestDirectory(), 8), foreign(InputTestDirectory(), 8);
        CHECK(core.Open(InputTestLocator(2), &old_receiver) == InputOpenResult::Opened);
        auto old = core.Capture(InputTestLocator(2));
        CHECK(InputTestDeliver(core, {}) == InputDeliveryResult::InvalidTicket);
        CHECK(InputTestDeliver(core, core.Capture(InputTestLocator(8))) == InputDeliveryResult::InvalidTicket);
        CHECK(InputTestDeliver(foreign, old) == InputDeliveryResult::InvalidTicket);
        auto old_storage = InputChannelsTestAccess::Entry(old);
        core.Close(InputTestLocator(2));
        CHECK(core.Open(InputTestLocator(2), &new_receiver) == InputOpenResult::Opened);
        auto fresh = core.Capture(InputTestLocator(2));
        CHECK(!InputChannelsTestAccess::SameEntry(old, fresh));
        CHECK(InputTestDeliver(core, old) == InputDeliveryResult::Closed);
        CHECK(InputTestDeliver(core, fresh) == InputDeliveryResult::Delivered);
        CHECK(old_receiver.calls == 0 && new_receiver.calls == 1);
        old = {};
        CHECK(old_storage.expired());
        // A new key-only close targets the current registration by contract.
        core.Close(InputTestLocator(2));
        CHECK(InputTestDeliver(core, fresh) == InputDeliveryResult::Closed);
        for (int i = 0; i < 100; ++i) {
            CHECK(core.Open(InputTestLocator(2), &new_receiver) == InputOpenResult::Opened);
            auto ticket = core.Capture(InputTestLocator(2));
            auto storage = InputChannelsTestAccess::Entry(ticket);
            core.Close(InputTestLocator(2)); ticket = {};
            CHECK(storage.expired());
        }
    });
    cases.emplace_back("IC-06-paused-callback-close-and-alias-rejection", [] {
        InputTestEvent entered, release, closing;
        InputTestReceiver receiver, replacement;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), {},
                [&](const Locator&) { closing.Signal(); });
        receiver.receive = [&](const std::uint8_t*, std::uint32_t, const Locator&, const Locator&) {
            entered.Signal(); release.Wait();
        };
        CHECK(core->Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        const auto ticket = core->Capture(InputTestLocator(2));
        auto delivery = std::async(std::launch::async, [&] { return InputTestDeliver(*core, ticket); });
        entered.Wait();
        std::atomic<bool> close_returned{false};
        auto close = std::async(std::launch::async, [&] { core->Close(InputTestLocator(2)); close_returned = true; });
        closing.Wait();
        const auto usage = core->SnapshotUsage();
        CHECK(usage.closing_channels == 1 && usage.in_flight == 1 && !close_returned);
        CHECK(InputTestDeliver(*core, ticket) == InputDeliveryResult::Closed);
        CHECK(InputTestDeliver(*core, core->Capture(InputTestLocator(2))) == InputDeliveryResult::InvalidTicket);
        CHECK(core->Open(InputTestLocator(2), &replacement) == InputOpenResult::Closing);
        CHECK(core->Open(InputTestLocator(3), &receiver) == InputOpenResult::Conflict);
        release.Signal();
        CHECK(Finish(delivery) == InputDeliveryResult::Delivered);
        Finish(close);
        CHECK(close_returned && core->SnapshotUsage().in_flight == 0);
        CHECK(core->Open(InputTestLocator(3), &receiver) == InputOpenResult::Opened);
    });
    cases.emplace_back("IC-07-callback-exception-releases-lease", [] {
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8);
        receiver.receive = [](const std::uint8_t*, std::uint32_t, const Locator&, const Locator&) { throw 42; };
        CHECK(core.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        CHECK(InputTestDeliver(core, core.Capture(InputTestLocator(2))) == InputDeliveryResult::CallbackFailure);
        CHECK(receiver.calls == 1 && core.SnapshotUsage().in_flight == 0);
        core.Close(InputTestLocator(2));
        CHECK(core.SnapshotUsage().open_channels == 0);
    });
    cases.emplace_back("IC-08-two-old-close-waiters-cannot-clear-reopen", [] {
        InputTestEvent entered, release, first_captured, second_captured, resume_second;
        std::atomic<int> close_count{0};
        InputTestReceiver old_receiver, new_receiver;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), {}, [&](const Locator&) {
            const int ordinal = ++close_count;
            if (ordinal == 1) first_captured.Signal();
            if (ordinal == 2) { second_captured.Signal(); resume_second.Wait(); }
        });
        old_receiver.receive = [&](const std::uint8_t*, std::uint32_t, const Locator&, const Locator&) {
            entered.Signal(); release.Wait();
        };
        CHECK(core->Open(InputTestLocator(2), &old_receiver) == InputOpenResult::Opened);
        auto old = core->Capture(InputTestLocator(2));
        auto delivery = std::async(std::launch::async, [&] { return InputTestDeliver(*core, old); });
        entered.Wait();
        auto first = std::async(std::launch::async, [&] { core->Close(InputTestLocator(2)); });
        first_captured.Wait();
        auto second = std::async(std::launch::async, [&] { core->Close(InputTestLocator(2)); });
        second_captured.Wait();
        CHECK(core->SnapshotUsage().in_flight == 1);
        release.Signal();
        CHECK(Finish(delivery) == InputDeliveryResult::Delivered);
        Finish(first);
        CHECK(core->Open(InputTestLocator(2), &new_receiver) == InputOpenResult::Opened);
        auto fresh = core->Capture(InputTestLocator(2));
        resume_second.Signal(); Finish(second);
        CHECK(InputTestDeliver(*core, fresh) == InputDeliveryResult::Delivered);
        CHECK(InputTestDeliver(*core, old) == InputDeliveryResult::Closed);
        CHECK(new_receiver.calls == 1 && core->SnapshotUsage().open_channels == 1);
    });
    cases.emplace_back("IC-09-repeated-disable-drains-closing-and-open-channels", [] {
        InputTestEvent entered, release_a, release_b, closing, disabling;
        InputTestReceiver a, b;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), {},
                [&](const Locator&) { closing.Signal(); }, [&] { disabling.Signal(); });
        a.receive = [&](const std::uint8_t*, std::uint32_t, const Locator&, const Locator&) {
            entered.Signal(); release_a.Wait();
        };
        b.receive = [&](const std::uint8_t*, std::uint32_t, const Locator&, const Locator&) {
            entered.Signal(); release_b.Wait();
        };
        CHECK(core->Open(InputTestLocator(2), &a) == InputOpenResult::Opened);
        CHECK(core->Open(InputTestLocator(3), &b) == InputOpenResult::Opened);
        auto ta = core->Capture(InputTestLocator(2)), tb = core->Capture(InputTestLocator(3));
        auto da = std::async(std::launch::async, [&] { return InputTestDeliver(*core, ta); });
        auto db = std::async(std::launch::async, [&] { return InputTestDeliver(*core, tb); });
        entered.Wait(2);
        auto close = std::async(std::launch::async, [&] { core->Close(InputTestLocator(2)); });
        closing.Wait();
        std::atomic<int> returned{0};
        auto first = std::async(std::launch::async, [&] { core->DisableAll(); ++returned; });
        disabling.Wait();
        auto second = std::async(std::launch::async, [&] { core->DisableAll(); ++returned; });
        disabling.Wait(2);
        auto usage = core->SnapshotUsage();
        CHECK(usage.disabled && !usage.drained && usage.in_flight == 2 && returned == 0);
        CHECK(core->Open(InputTestLocator(2), &a) == InputOpenResult::Disabled);
        CHECK(InputTestDeliver(*core, ta) == InputDeliveryResult::Disabled);
        CHECK(InputTestDeliver(*core, tb) == InputDeliveryResult::Disabled);
        release_a.Signal(); CHECK(Finish(da) == InputDeliveryResult::Delivered); Finish(close);
        usage = core->SnapshotUsage();
        CHECK(usage.in_flight == 1 && !usage.drained && returned == 0);
        release_b.Signal(); CHECK(Finish(db) == InputDeliveryResult::Delivered);
        Finish(first); Finish(second);
        usage = core->SnapshotUsage();
        CHECK(returned == 2 && usage.drained && usage.open_channels == 0 && usage.closing_channels == 0);
        core->DisableAll();
        CHECK(core->Open(InputTestLocator(3), &b) == InputOpenResult::Disabled);
        CHECK(InputTestDeliver(*core, tb) == InputDeliveryResult::Disabled);
    });
    cases.emplace_back("IC-10-destruction-retained-ticket-and-core-address-reuse", [] {
        struct OwnedReceiver : InputTestReceiver {
            bool* destroyed;
            explicit OwnedReceiver(bool* flag) : destroyed(flag) {}
            ~OwnedReceiver() override { *destroyed = true; }
        };
        bool destroyed = false;
        auto receiver = std::make_unique<OwnedReceiver>(&destroyed);
        alignas(InputChannels) unsigned char storage[sizeof(InputChannels)];
        auto* first = new (storage) InputChannels(InputTestDirectory(), 8);
        CHECK(first->Open(InputTestLocator(2), receiver.get()) == InputOpenResult::Opened);
        auto old = first->Capture(InputTestLocator(2));
        first->~InputChannels();
        CHECK(!destroyed);
        receiver.reset(); CHECK(destroyed);
        auto* next = new (storage) InputChannels(InputTestDirectory(), 8);
        InputTestReceiver replacement;
        CHECK(next->Open(InputTestLocator(2), &replacement) == InputOpenResult::Opened);
        auto fresh = next->Capture(InputTestLocator(2));
        CHECK(!InputChannelsTestAccess::SameIdentity(old, fresh));
        CHECK(InputTestDeliver(*next, old) == InputDeliveryResult::InvalidTicket);
        CHECK(InputTestDeliver(*next, fresh) == InputDeliveryResult::Delivered);
        next->~InputChannels();
        CHECK(replacement.calls == 1);
    });

    cases.emplace_back("IC-11-route-copy-allocation-failure", [] {
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8);
        const auto local = InputTestLocator(2), remote = InputTestLocator(4);
        CHECK(core.Open(local, &receiver) == InputOpenResult::Opened);
        const auto ticket = core.Capture(local);
        const std::uint8_t bytes[] = {1, 2, 3};
        const auto failures_before = input_allocation_failures;
        auto result = InputDeliveryResult::Delivered;
        try {
            fail_next_input_allocation = true;
            result = core.Deliver(ticket, remote, bytes, sizeof(bytes));
            fail_next_input_allocation = false;
        } catch (...) {
            fail_next_input_allocation = false;
            core.Close(local);
            throw;
        }
        CHECK(input_allocation_failures == failures_before + 1);
        CHECK(result == InputDeliveryResult::AllocationFailure);
        CHECK(receiver.calls == 0 && core.SnapshotUsage().in_flight == 0);
        CHECK(core.Deliver(ticket, remote, bytes, sizeof(bytes)) == InputDeliveryResult::Delivered);
        CHECK(receiver.calls == 1);
        core.Close(local);
        CHECK(core.SnapshotUsage().open_channels == 0);
    });

    cases.emplace_back("IC-12-construction-allocation-failures-propagate", [] {
        // Turn an unexpected noexcept termination into an explicit failing test
        // exit without a Windows crash dialog. Never part of production cleanup.
        struct RestoreTerminate {
            std::terminate_handler previous;
            ~RestoreTerminate() { std::set_terminate(previous); }
        } restore{std::set_terminate([] {
            std::fputs("FAIL IC-12 unexpected std::terminate\n", stderr);
            std::_Exit(86);
        })};
        const auto directory = InputTestDirectory();
        InputTestReceiver receiver;
        InputChannels existing(directory, 8);
        CHECK(existing.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        const auto ticket = existing.Capture(InputTestLocator(2));
        std::unique_ptr<InputChannels> candidate;
        input_allocations_observed = 0;
        count_input_allocations = true;
        candidate = std::make_unique<InputChannels>(directory, 8);
        count_input_allocations = false;
        const auto count = input_allocations_observed;
        candidate.reset();
        CHECK(count > 0 && count < 64);
        for (std::size_t index = 0; index < count; ++index) {
            const auto failures_before = input_allocation_failures;
            bool caught = false;
            std::cout << "IC-12 construction allocation " << index << '/' << count << std::endl;
            try {
                input_fail_after = static_cast<std::ptrdiff_t>(index);
                candidate = std::make_unique<InputChannels>(directory, 8);
                input_fail_after = -1;
            } catch (const std::bad_alloc&) {
                input_fail_after = -1;
                caught = true;
            } catch (...) {
                input_fail_after = -1;
                throw;
            }
            CHECK(caught && !candidate);
            CHECK(input_allocation_failures == failures_before + 1);
            CHECK(existing.SnapshotUsage().open_channels == 1);
            CHECK(InputTestDeliver(existing, ticket) == InputDeliveryResult::Delivered);
        }
        candidate = std::make_unique<InputChannels>(directory, 8);
        CHECK(candidate->SnapshotUsage().open_channels == 0);
        candidate.reset();
        existing.Close(InputTestLocator(2));
        CHECK(receiver.calls == count);
    });

    cases.emplace_back("IC-N3-01-permit-before-close-pins-original", [] {
        InputTestEvent close_captured;
        InputTestReceiver old_receiver, new_receiver;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), {},
                [&](const Locator&) { close_captured.Signal(); });
        const auto local = InputTestLocator(2), remote = InputTestLocator(4);
        CHECK(core->Open(local, &old_receiver) == InputOpenResult::Opened);
        auto ticket = core->Capture(local);
        auto lease = core->TryAcquireReceiver(ticket);
        CHECK(lease && core->SnapshotUsage().in_flight == 1);
        auto closing = std::async(std::launch::async, [&] { core->Close(local); });
        close_captured.Wait();
        CHECK(closing.wait_for(0s) == std::future_status::timeout);
        CHECK(!core->TryAcquireReceiver(ticket));
        const std::uint8_t bytes[]{1, 2};
        CHECK(lease.Deliver(remote, bytes, sizeof(bytes)) == InputDeliveryResult::Delivered);
        CHECK(!lease);
        Finish(closing);
        CHECK(core->SnapshotUsage().in_flight == 0 && old_receiver.calls == 1);
        CHECK(core->Open(local, &new_receiver) == InputOpenResult::Opened);
        CHECK(!core->TryAcquireReceiver(ticket));
        CHECK(lease.Deliver(remote, bytes, sizeof(bytes)) == InputDeliveryResult::InvalidTicket);
        CHECK(new_receiver.calls == 0);
    });
    cases.emplace_back("IC-N3-02-abandon-move-and-no-allocation", [] {
        static_assert(!std::is_copy_constructible_v<InputReceiverLease>);
        static_assert(std::is_nothrow_move_constructible_v<InputReceiverLease>);
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8);
        CHECK(core.Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        const auto ticket = core.Capture(InputTestLocator(2));
        count_input_allocations = true; input_allocations_observed = 0;
        auto first = core.TryAcquireReceiver(ticket);
        const auto acquired_allocations = input_allocations_observed;
        count_input_allocations = false;
        CHECK(first && acquired_allocations == 0);
        auto second = core.TryAcquireReceiver(ticket);
        CHECK(second && core.SnapshotUsage().in_flight == 2);
        second = std::move(first);
        CHECK(!first && second && core.SnapshotUsage().in_flight == 1);
        second = {};
        CHECK(core.SnapshotUsage().in_flight == 0 && receiver.calls == 0);
        core.Close(InputTestLocator(2));
    });
    cases.emplace_back("IC-N3-03-permit-survives-core-destruction-entry", [] {
        InputTestEvent disabled;
        InputTestReceiver receiver;
        auto core = InputChannelsTestAccess::Make(InputTestDirectory(), {}, {}, [&] { disabled.Signal(); });
        CHECK(core->Open(InputTestLocator(2), &receiver) == InputOpenResult::Opened);
        auto lease = core->TryAcquireReceiver(core->Capture(InputTestLocator(2)));
        CHECK(lease);
        auto destruction = std::async(std::launch::async, [&] { core.reset(); });
        disabled.Wait();
        CHECK(destruction.wait_for(0s) == std::future_status::timeout);
        const std::uint8_t byte = 7;
        CHECK(lease.Deliver(InputTestLocator(4), &byte, 1) == InputDeliveryResult::Delivered);
        Finish(destruction);
        CHECK(receiver.calls == 1 && !lease);
    });
    cases.emplace_back("IC-N3-04-failure-settles-real-lease", [] {
        InputTestReceiver receiver;
        InputChannels core(InputTestDirectory(), 8), foreign(InputTestDirectory(), 8);
        const auto local = InputTestLocator(2), remote = InputTestLocator(4);
        CHECK(core.Open(local, &receiver) == InputOpenResult::Opened);
        auto ticket = core.Capture(local);
        CHECK(!foreign.TryAcquireReceiver(ticket));
        const std::uint8_t bytes[]{1, 2};
        auto invalid = core.TryAcquireReceiver(ticket);
        CHECK(invalid.Deliver(remote, nullptr, 1) == InputDeliveryResult::InvalidBuffer);
        CHECK(core.SnapshotUsage().in_flight == 0);
        auto oversized = core.TryAcquireReceiver(ticket);
        CHECK(oversized.Deliver(remote, bytes, 9) == InputDeliveryResult::TooLarge);
        auto bad_route = core.TryAcquireReceiver(ticket);
        CHECK(bad_route.Deliver(InputTestLocator(8), bytes, 2) == InputDeliveryResult::InvalidRoute);
        CHECK(core.SnapshotUsage().in_flight == 0 && receiver.calls == 0);
        receiver.receive = [](const auto*, auto, const auto&, const auto&) { throw std::runtime_error("receiver fault"); };
        auto throwing = core.TryAcquireReceiver(ticket);
        CHECK(throwing.Deliver(remote, bytes, 2) == InputDeliveryResult::CallbackFailure);
        CHECK(core.SnapshotUsage().in_flight == 0 && receiver.calls == 1);
        CHECK(throwing.Deliver(remote, bytes, 2) == InputDeliveryResult::InvalidTicket);
    });

    std::size_t run = 0;
    for (const auto& test : cases) {
        if (argc == 2 && test.first.find(argv[1]) == std::string::npos) continue;
        ++run;
        try { test.second(); std::cout << "PASS " << test.first << std::endl; }
        catch (const std::exception& error) {
            std::cerr << "FAIL " << test.first << ": " << error.what() << std::endl;
            return 1;
        }
    }
    if (run == 0) return 2;
    std::cout << "PASS " << run << " input channel cases" << std::endl;
    return 0;
}

int main(int argc, char** argv) { return RunInputChannelsTests(argc, argv); }
