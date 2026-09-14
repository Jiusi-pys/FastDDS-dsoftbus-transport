#include "fastdds_interface_probe.hpp"

#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using namespace mdds_probe;
using namespace std::chrono_literals;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
        std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

rtps::Locator target(unsigned char id)
{
    rtps::Locator result(interface_probe_kind, 1);
    result.address[15] = id;
    return result;
}

bool send(rtps::SenderResource& resource, const std::vector<rtps::NetworkBuffer>& buffers,
        uint32_t size, const std::vector<rtps::Locator>& targets, const Deadline& deadline, int32_t priority = 0)
{
    rtps::Locators begin(targets.cbegin()), end(targets.cend());
    return resource.send(buffers, size, &begin, &end, deadline, priority);
}

void factory_test()
{
    static_assert(!std::is_abstract<InterfaceProbeTransport>::value, "current pure virtual signatures must match");
    InterfaceProbeDescriptor descriptor(8192);
    std::unique_ptr<rtps::TransportInterface> transport(descriptor.create_transport());
    CHECK(transport != nullptr);
    CHECK(transport->init());
    CHECK(transport->kind() == interface_probe_kind);
    CHECK(transport->get_configuration() != &descriptor);
    CHECK(transport->max_recv_buffer_size() == 8192);
    CHECK(transport->get_configuration()->min_send_buffer_size() == 8192);
    descriptor.maxMessageSize = 16384;
    CHECK(transport->get_configuration()->maxMessageSize == 8192);
    rtps::SendResourceList senders;
    CHECK(transport->OpenOutputChannel(senders, target(2)));
    CHECK(senders.size() == 1);
    CHECK(senders.front()->kind() == interface_probe_kind);
}

void sender_parameters_test()
{
    auto observation = std::make_shared<ProbeSendObservation>();
    ProbeSenderResource sender(observation, 8);
    rtps::octet first[] = {1, 2};
    rtps::octet second[] = {3, 4, 5, 6};
    std::vector<rtps::Locator> targets = {target(2), target(3)};
    const auto expected_targets = targets;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    CHECK(send(sender, {{first, 2}, {nullptr, 0}, {second, 4}}, 5, targets, deadline, 37));
    CHECK(observation->records.size() == 1);
    first[0] = 99;
    second[0] = 98;
    targets[0].address[15] = 77;
    const auto& record = observation->records.front();
    CHECK(record.bytes == std::vector<rtps::octet>({1, 2, 3, 4, 5}));
    CHECK(record.targets == expected_targets);
    CHECK(record.deadline == deadline);
    CHECK(record.priority == 37);

    CHECK(!send(sender, {{first, 2}}, 3, targets, deadline));
    CHECK(!send(sender, {{nullptr, 1}}, 1, targets, deadline));
    CHECK(!send(sender, {{first, 2}}, 9, targets, deadline));
    CHECK(!send(sender, {{first, 2}}, UINT32_MAX, targets, deadline));
    CHECK(!send(sender, {}, 0, targets, deadline));
    CHECK(!send(sender, {{first, 2}}, 2, {}, deadline));
    CHECK(!send(sender, {{first, 2}}, 2, targets, std::chrono::steady_clock::now() - 1s));
    auto bad_targets = targets;
    bad_targets[1].kind = LOCATOR_KIND_UDPv4;
    CHECK(!send(sender, {{first, 2}}, 2, bad_targets, deadline));
    CHECK(observation->records.size() == 1); // Rejections never partially record.

    // total_bytes is a prefix boundary: unread trailing buffers need not be dereferenced.
    CHECK(send(sender, {{first, 2}, {nullptr, UINT32_MAX}}, 2, targets, deadline, -3));
    CHECK(observation->records.back().bytes == std::vector<rtps::octet>({99, 2}));
    CHECK(observation->records.back().priority == -3);
}

void cleanup_test()
{
    static_assert(!std::is_copy_constructible<ProbeSenderResource>::value, "resource cannot be copied");
    static_assert(!std::is_copy_assignable<ProbeSenderResource>::value, "resource cannot be copy assigned");
    static_assert(!std::is_move_constructible<ProbeSenderResource>::value, "this-capturing callbacks cannot move");
    static_assert(!std::is_move_assignable<ProbeSenderResource>::value, "this-capturing callbacks cannot move assign");
    auto observation = std::make_shared<ProbeSendObservation>();
    {
        ProbeSenderResource sender(observation, 8);
        sender.release();
        sender.release();
        CHECK(observation->cleanup_count == 1);
        const rtps::octet data[] = {1};
        CHECK(!send(sender, {{data, 1}}, 1, {target(2)}, std::chrono::steady_clock::now() + 1s));
    }
    CHECK(observation->cleanup_count == 1);
    {
        std::unique_ptr<rtps::SenderResource> sender(new ProbeSenderResource(observation, 8));
    }
    CHECK(observation->cleanup_count == 2); // Destruction through real base type.

    InterfaceProbeDescriptor descriptor;
    rtps::SendResourceList resources;
    {
        InterfaceProbeTransport transport(descriptor);
        CHECK(transport.init());
        CHECK(transport.OpenOutputChannel(resources, target(2)));
    }
    // Observation storage and cleanup state survive the transport object.
    resources.clear();
    CHECK(descriptor.observation->cleanup_count == 1);
}

class BlockingReceiver final : public rtps::TransportReceiverInterface
{
public:
    InterfaceProbeTransport* transport = nullptr;
    const rtps::octet* caller_buffer = nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    bool timeout = false;
    bool open_in_callback = false;
    bool owns_buffer = false;
    unsigned calls = 0;
    rtps::Locator local, remote;
    std::vector<rtps::octet> before, after;

    void OnDataReceived(const rtps::octet* data, const uint32_t size,
            const rtps::Locator& local_value, const rtps::Locator& remote_value) override
    {
        // This takes the transport lock itself: callback-under-lock would deadlock.
        open_in_callback = transport->IsInputChannelOpen(local_value);
        std::unique_lock<std::mutex> lock(mutex);
        ++calls;
        owns_buffer = data != caller_buffer;
        local = local_value;
        remote = remote_value;
        before.assign(data, data + size);
        entered = true;
        changed.notify_all();
        timeout = !changed.wait_for(lock, 3s, [this] { return release; });
        after.assign(data, data + size);
    }
};

void receive_close_test()
{
    InterfaceProbeDescriptor descriptor;
    InterfaceProbeTransport transport(descriptor);
    CHECK(transport.init());
    BlockingReceiver receiver;
    receiver.transport = &transport;
    rtps::octet source[] = {10, 20, 30};
    receiver.caller_buffer = source;
    CHECK(!transport.OpenInputChannel(descriptor.local, nullptr, 64));
    CHECK(transport.OpenInputChannel(descriptor.local, &receiver, 64));
    CHECK(transport.OpenInputChannel(descriptor.local, &receiver, 64));
    BlockingReceiver other_receiver;
    CHECK(!transport.OpenInputChannel(descriptor.local, &other_receiver, 64));
    CHECK(transport.IsInputChannelOpen(descriptor.local));
    CHECK(!transport.deliver(nullptr, 3, target(2)));
    CHECK(!transport.deliver(source, 65, target(2)));
    const auto remote = target(2);
    auto delivering = std::async(std::launch::async, [&] { return transport.deliver(source, 3, remote); });
    bool entered;
    {
        std::unique_lock<std::mutex> lock(receiver.mutex);
        entered = receiver.changed.wait_for(lock, 1s, [&] { return receiver.entered; });
    }
    CHECK(entered);
    source[0] = 99; // Callback must retain its own input bytes until it returns.
    std::promise<void> close_started;
    auto close_start = close_started.get_future();
    auto closing = std::async(std::launch::async, [&]
            {
                close_started.set_value();
                return transport.CloseInputChannel(descriptor.local);
            });
    close_start.wait();
    const bool close_waits = closing.wait_for(50ms) == std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(receiver.mutex);
        receiver.release = true;
        receiver.changed.notify_all();
    }
    CHECK(delivering.get());
    CHECK(closing.get());
    CHECK(close_waits);
    CHECK(!receiver.timeout);
    CHECK(receiver.open_in_callback);
    CHECK(receiver.owns_buffer);
    CHECK(receiver.local == descriptor.local);
    CHECK(receiver.remote == remote);
    CHECK(receiver.before == std::vector<rtps::octet>({10, 20, 30}));
    CHECK(receiver.after == receiver.before);
    CHECK(!transport.IsInputChannelOpen(descriptor.local));
    CHECK(!transport.deliver(source, 3, remote));
    CHECK(receiver.calls == 1);
    CHECK(transport.CloseInputChannel(descriptor.local));
    transport.shutdown();
    transport.shutdown();
    CHECK(!transport.OpenInputChannel(descriptor.local, &receiver, 64));
}

struct TestCase { const char* id; void (*run)(); };
} // namespace

int main(int argc, char** argv)
{
    const TestCase cases[] = {{"IF-01", factory_test}, {"IF-02", sender_parameters_test},
        {"IF-03", cleanup_test}, {"IF-04", receive_close_test}};
    if (argc > 2)
    {
        std::cerr << "usage: fastdds_interface_tests [IF-01|IF-02|IF-03|IF-04]\n";
        return 2;
    }
    unsigned ran = 0, failed = 0;
    for (const auto& test : cases)
    {
        if (argc == 2 && std::string(argv[1]) != test.id) continue;
        ++ran;
        std::cout << test.id << " RUN\n" << std::flush;
        try
        {
            test.run();
            std::cout << test.id << " PASS\n";
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << test.id << " FAIL " << error.what() << '\n';
        }
    }
    if (ran == 0)
    {
        std::cerr << "unknown test ID\n";
        return 2;
    }
    std::cout << "S1 interface probe: " << ran << " executed, " << failed << " failed; no SDK/DDS integration claim\n";
    return failed == 0 ? 0 : 1;
}
