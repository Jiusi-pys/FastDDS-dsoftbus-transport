#include <mdds/dsoftbus/ApplicationAttachments.hpp>
#include <fastdds/utils/IPLocator.hpp>

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
using namespace mdds::dsoftbus;
namespace rtps = eprosima::fastdds::rtps;
#define ATTACHMENT_CHECK(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

class AttachmentConsumerReceiver final : public rtps::TransportReceiverInterface
{
public:
    void OnDataReceived(const rtps::octet* data, const std::uint32_t size,
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
    // These out-of-line functions and returned string cross the actual Fast DDS
    // shared-library ABI. This fixture IP is never used as a DSoftBus route.
    Locator ip;
    rtps::IPLocator::setIPv4(ip, 127, 0, 0, 1);
    ATTACHMENT_CHECK(rtps::IPLocator::toIPv4string(ip) == "127.0.0.1");
#ifdef _WIN32
    const auto module = GetModuleHandleW(L"fastdds-3.6.dll");
    ATTACHMENT_CHECK(module != nullptr);
    wchar_t module_path[32768]{};
    const auto length = GetModuleFileNameW(module, module_path, 32768);
    ATTACHMENT_CHECK(length > 0 && length < 32768);
    const auto count = WideCharToMultiByte(CP_UTF8, 0, module_path,
            static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    ATTACHMENT_CHECK(count > 0);
    std::string loaded_path(static_cast<std::size_t>(count), '\0');
    ATTACHMENT_CHECK(WideCharToMultiByte(CP_UTF8, 0, module_path,
            static_cast<int>(length), &loaded_path[0], count, nullptr, nullptr) == count);
    std::cout << "LOADED_FASTDDS=" << loaded_path << '\n';
#endif

    EndpointId local_id{}, remote_id{};
    local_id.back() = 2;
    remote_id.back() = 4;
    Locator local, remote;
    ATTACHMENT_CHECK(LocatorCodec::Encode(local_id, local));
    ATTACHMENT_CHECK(LocatorCodec::Encode(remote_id, remote));
    DirectoryConfiguration config;
    config.locals.push_back({local, "consumer.namespace", "consumer.listen"});
    Route route;
    route.key = {local, remote};
    route.fields = {"consumer-network", "consumer.remote.listen", "consumer.dial",
        {"consumer.remote.dial"}, RecordDataType::Bytes};
    config.routes.push_back(route);
    const auto directory = EndpointDirectory::Build(config);
    ATTACHMENT_CHECK(directory.value);

    // Receivers outlive attachments, including exceptional stack unwinding.
    AttachmentConsumerReceiver old_receiver, new_receiver;
    ApplicationAttachments attachments({local});
    auto old_claim = attachments.ClaimApplication({local}, *directory.value);
    ATTACHMENT_CHECK(old_claim.result == ApplicationClaimResult::Claimed && old_claim.claim);
    auto old_binding = attachments.OpenReceiver(old_claim.claim, local, &old_receiver);
    ATTACHMENT_CHECK(old_binding.result == ReceiverOpenResult::Opened && old_binding.binding);
    const std::vector<std::uint8_t> expected{0x52, 0x54, 0, 0xff};
    std::array<std::uint8_t, 4> source{{0x52, 0x54, 0, 0xff}};
    ATTACHMENT_CHECK(attachments.AdmitReceive(local, remote, source.data(), source.size()) ==
            ReceiveAdmissionResult::Accepted);
    source.fill(0); // The public queue, not this caller, now owns the record.
    auto work = attachments.TryTake();
    ATTACHMENT_CHECK(work && work.Deliver() == InputDeliveryResult::Delivered);
    ATTACHMENT_CHECK(old_receiver.calls == 1 && old_receiver.bytes == expected);
    ATTACHMENT_CHECK(old_receiver.observed_local == local && old_receiver.observed_remote == remote);
    ATTACHMENT_CHECK(!attachments.TryTake());

    ATTACHMENT_CHECK(attachments.AdmitReceive(local, remote, expected.data(), expected.size()) ==
            ReceiveAdmissionResult::Accepted);
    auto old_taken = attachments.TryTake();
    ATTACHMENT_CHECK(old_taken);
    ATTACHMENT_CHECK(attachments.RequestDetach(old_claim.claim) == AttachmentControlResult::Requested);
    const auto detached = attachments.AwaitDetached(old_claim.claim);
    ATTACHMENT_CHECK(detached == AttachmentControlResult::Completed ||
            detached == AttachmentControlResult::AlreadyDetached);
    ATTACHMENT_CHECK(attachments.SnapshotUsage().active_applications == 0);

    // Reclaim exactly the same local slot/configuration; no renamed identifiers.
    // This is an application-core model, not a real DDS Participant hot rebuild.
    auto new_claim = attachments.ClaimApplication({local}, *directory.value);
    ATTACHMENT_CHECK(new_claim.result == ApplicationClaimResult::Claimed && new_claim.claim);
    auto new_binding = attachments.OpenReceiver(new_claim.claim, local, &new_receiver);
    ATTACHMENT_CHECK(new_binding.result == ReceiverOpenResult::Opened && new_binding.binding);
    ATTACHMENT_CHECK(!(new_binding.binding == old_binding.binding));
    ATTACHMENT_CHECK(old_taken.Deliver() == InputDeliveryResult::Disabled);
    ATTACHMENT_CHECK(old_receiver.calls == 1 && new_receiver.calls == 0);
    ATTACHMENT_CHECK(attachments.CloseReceiver(old_binding.binding) == AttachmentControlResult::Completed);
    ATTACHMENT_CHECK(attachments.AdmitReceive(local, remote, expected.data(), expected.size()) ==
            ReceiveAdmissionResult::Accepted);
    ATTACHMENT_CHECK(attachments.TryTake().Deliver() == InputDeliveryResult::Delivered);
    ATTACHMENT_CHECK(new_receiver.calls == 1 && new_receiver.bytes == expected);
    ATTACHMENT_CHECK(new_receiver.observed_local == local && new_receiver.observed_remote == remote);
    ATTACHMENT_CHECK(attachments.CloseReceiver(new_binding.binding) == AttachmentControlResult::Completed);
    ATTACHMENT_CHECK(attachments.AwaitDetached(new_claim.claim) == AttachmentControlResult::Completed);
    const auto usage = attachments.SnapshotUsage();
    ATTACHMENT_CHECK(usage.active_applications == 0 && usage.records == 0 && usage.bytes == 0 &&
            usage.copying == 0 && usage.queued == 0 && usage.taken == 0 && usage.delivering == 0);
    std::cout << "ATTACHMENTS_CONSUMER PASS upstream_string=1 owned_receive=1 old_work_revoked=1 "
        "same_slot_reclaim=1 model_only=1 sdk_calls=0 participants_created=0\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "ATTACHMENTS_CONSUMER FAIL " << error.what() << '\n';
    return 1;
}
