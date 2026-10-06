#include <fastdds_dsoftbus/dsoftbus/EndpointDirectory.hpp>
#include <fastdds_dsoftbus/dsoftbus/SendQueue.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusSenderResource.hpp>
#include <fastdds_dsoftbus/dsoftbus/InputChannels.hpp>
#include <fastdds/utils/IPLocator.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#if !defined(FASTDDS_DYN_LINK) || defined(FASTDDS_STATIC_LINK)
#error The CONFIG consumer must inherit the actual shared-library binding.
#endif

namespace {
using namespace fastdds_dsoftbus::dsoftbus;
namespace rtps = eprosima::fastdds::rtps;
#define BINDING_CHECK(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

class BindingGenerationSource final : public ConnectionGenerationSource
{
public:
    bool TryResolve(const Route& route, SendConnectionSnapshot& output) const override
    {
        output = {route.key, route.route_version, 7};
        return true; // Explicit model source, not an SDK connection.
    }
};

class BindingReceiver final : public rtps::TransportReceiverInterface
{
public:
    void OnDataReceived(const rtps::octet* data, const uint32_t size,
            const Locator& local, const Locator& remote) override
    {
        bytes.assign(data, data + size);
        observed_local = local;
        observed_remote = remote;
        ++calls;
    }
    std::vector<std::uint8_t> bytes;
    Locator observed_local, observed_remote;
    unsigned calls = 0;
};
}

int main()
try
{
    // Out-of-line upstream calls and a returned std::string cross the real DLL ABI.
    // This fixture-only IP value is never a DSoftBus route or a networking operation.
    Locator ip;
    eprosima::fastdds::rtps::IPLocator::setIPv4(ip, 127, 0, 0, 1);
    BINDING_CHECK(eprosima::fastdds::rtps::IPLocator::toIPv4string(ip) == "127.0.0.1");
#ifdef _WIN32
    wchar_t module_path[32768]{};
    const auto module = GetModuleHandleW(L"fastdds-3.6.dll");
    const auto length = GetModuleFileNameW(module, module_path, 32768);
    BINDING_CHECK(module && length > 0 && length < 32768);
    const auto count = WideCharToMultiByte(CP_UTF8, 0, module_path,
            static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    BINDING_CHECK(count > 0);
    std::string path(static_cast<std::size_t>(count), '\0');
    BINDING_CHECK(WideCharToMultiByte(CP_UTF8, 0, module_path,
            static_cast<int>(length), &path[0], count, nullptr, nullptr) == count);
    std::cout << "LOADED_FASTDDS=" << path << '\n';
#endif

    EndpointId local_id{}, remote_id{};
    local_id.back() = 2;
    remote_id.back() = 10;
    Locator local, remote;
    BINDING_CHECK(LocatorCodec::Encode(local_id, local));
    BINDING_CHECK(LocatorCodec::Encode(remote_id, remote));
    DirectoryConfiguration config;
    config.locals.push_back({local, "binding.namespace", "binding.listen"});
    Route route;
    route.key = {local, remote};
    route.fields = {"binding-network", "binding.remote.listen", "binding.dial",
            {"binding.remote.dial"}, RecordDataType::Bytes};
    config.routes.push_back(route);
    const auto plan = EgressPlan::Build(config, {});
    BINDING_CHECK(plan.value && plan.value->size() == 1);
    auto runtime = std::make_shared<RuntimeSendPath>(plan.value,
            SendQueueLimits{4, 4096, 4, 4096, 4096}, std::make_shared<BindingGenerationSource>());
    rtps::SendResourceList senders;
    BINDING_CHECK(DSoftBusSenderResource::Attach(senders, runtime).status == SenderAttachStatus::Attached);
    BINDING_CHECK(DSoftBusSenderResource::Attach(senders, runtime).status == SenderAttachStatus::AlreadyAttached);
    std::array<std::uint8_t, 4> payload{{1, 2, 3, 4}};
    const std::vector<rtps::NetworkBuffer> buffers{{payload.data(), 4}};
    const std::vector<Locator> destinations{remote, remote};
    rtps::Locators begin(destinations.cbegin()), end(destinations.cend());
    const auto deadline = SendQueue::Clock::now() + std::chrono::seconds(1);
    BINDING_CHECK(senders.front()->send(buffers, 4, &begin, &end, deadline, -3));
    payload.fill(0);
    auto record = runtime->TryTake();
    const std::array<std::uint8_t, 4> expected{{1, 2, 3, 4}};
    BINDING_CHECK(record && record->size() == 4 && record->generation() == 7);
    BINDING_CHECK(record->priority() == -3 && record->deadline() == deadline);
    BINDING_CHECK(std::equal(expected.begin(), expected.end(), record->data()));
    BINDING_CHECK(!runtime->TryTake());

    BindingReceiver receiver;
    InputChannels inputs(plan.value->directory(), 4096);
    BINDING_CHECK(inputs.Open(local, &receiver) == InputOpenResult::Opened);
    auto ticket = inputs.Capture(local);
    BINDING_CHECK(inputs.Deliver(ticket, remote, record->data(), record->size()) == InputDeliveryResult::Delivered);
    BINDING_CHECK(receiver.calls == 1 && receiver.bytes == std::vector<std::uint8_t>(expected.begin(), expected.end()));
    BINDING_CHECK(receiver.observed_local == local && receiver.observed_remote == remote);
    inputs.Close(local);
    BINDING_CHECK(inputs.Deliver(ticket, remote, record->data(), record->size()) == InputDeliveryResult::Closed);
    auto* sender = dynamic_cast<DSoftBusSenderResource*>(senders.front().get());
    BINDING_CHECK(sender && sender->Release() && !sender->Release());
    runtime->Close();
    BINDING_CHECK(runtime->SnapshotUsage().records == 1); // Taken bytes are still owned.
    record.reset();
    BINDING_CHECK(runtime->SnapshotUsage().records == 0);
    inputs.DisableAll();
    std::cout << "BINDING_CONSUMER PASS upstream_string=1 send=1 receive=1 release=once model_only=1\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "BINDING_CONSUMER FAIL " << error.what() << '\n';
    return 1;
}
