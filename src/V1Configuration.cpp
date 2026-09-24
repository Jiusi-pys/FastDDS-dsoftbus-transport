#include <mdds/dsoftbus/V1Configuration.hpp>
#include <limits>
#include <map>
#include <new>
#include <tuple>
#include <utility>

namespace mdds::dsoftbus::v1 {
namespace {
ConfigurationResult Reject(ConfigurationStatus status, std::optional<std::size_t> index = {},
        std::optional<std::size_t> prior = {})
{
    return {{}, status, index, prior};
}

bool ValidBudget(const ResourceBudget& budget) noexcept
{
    const auto maximum = std::numeric_limits<std::size_t>::max();
    return budget.messages && budget.messages <= maximum && budget.bytes && budget.bytes <= maximum &&
           budget.connections && budget.connections <= maximum && budget.workers && budget.workers <= maximum;
}

bool ValidStage(OptionStage stage) noexcept
{
    switch (stage) {
        case OptionStage::ListenQos:
        case OptionStage::BindQos:
        case OptionStage::SocketBeforeListen:
        case OptionStage::SocketBeforeBind:
        case OptionStage::SocketAfterReady:
            return true;
    }
    return false;
}

bool ValidDuration(std::chrono::milliseconds duration) noexcept
{
    return duration.count() > 0 && duration != std::chrono::milliseconds::max();
}
} // namespace

ConfigurationSnapshot::ConfigurationSnapshot(const ConfigurationInput& input, std::string name)
    : input_(input), service_name_(std::move(name)) {}

ConfigurationResult ConfigurationValidator::Validate(const ConfigurationInput& input)
{
    try {
        std::string service_name;
        if (!AddressCodec::EncodeServiceName(input.service, service_name))
            return Reject(ConfigurationStatus::InvalidService);
        if (input.intent != ConfigurationIntent::Probe && input.intent != ConfigurationIntent::Production)
            return Reject(ConfigurationStatus::InvalidIntent);
        if (input.qualification_profile.find('\0') != std::string::npos)
            return Reject(ConfigurationStatus::InvalidQualificationReference);
        if (input.intent == ConfigurationIntent::Production && input.qualification_profile.empty())
            return Reject(ConfigurationStatus::MissingQualificationReference);
        if ((input.tx_mode != TxMode::Queue && input.tx_mode != TxMode::Native) ||
                (input.rx_mode != RxMode::Queue && input.rx_mode != RxMode::Inline))
            return Reject(ConfigurationStatus::InvalidMode);
        // Fast DDS receiver length is uint32_t. No narrowing is performed.
        if (!input.max_message_size || input.max_message_size > std::numeric_limits<std::uint32_t>::max())
            return Reject(ConfigurationStatus::InvalidMessageSize);
        const auto& participant = input.participant_budget;
        const auto& process = input.process_budget;
        if (!ValidBudget(participant) || !ValidBudget(process))
            return Reject(ConfigurationStatus::InvalidBudget);
        if (input.max_message_size > participant.bytes || input.max_message_size > process.bytes)
            return Reject(ConfigurationStatus::MessageExceedsBudget);
        if (participant.messages > process.messages || participant.bytes > process.bytes ||
                participant.connections > process.connections || participant.workers > process.workers)
            return Reject(ConfigurationStatus::ParticipantExceedsProcess);
        const auto& operations = input.operations;
        if (!ValidDuration(operations.connect) || !ValidDuration(operations.queue) ||
                !ValidDuration(operations.native_send) || !ValidDuration(operations.close))
            return Reject(ConfigurationStatus::InvalidOperationLimit);

        std::map<std::uint32_t, std::size_t> peers;
        for (std::size_t i = 0; i < input.initial_peer_slots.size(); ++i) {
            const auto slot = input.initial_peer_slots[i];
            if (slot > AddressCodec::max_slot) return Reject(ConfigurationStatus::InvalidPeerSlot, i);
            const auto inserted = peers.emplace(slot, i);
            if (!inserted.second)
                return Reject(ConfigurationStatus::DuplicatePeerSlot, i, inserted.first->second);
        }

        using OptionKey = std::tuple<OptionStage, std::int32_t, std::int32_t>;
        std::map<OptionKey, std::size_t> options;
        for (std::size_t i = 0; i < input.options.size(); ++i) {
            const auto& option = input.options[i];
            if (!ValidStage(option.stage)) return Reject(ConfigurationStatus::InvalidOptionStage, i);
            if ((option.stage == OptionStage::ListenQos || option.stage == OptionStage::BindQos) && option.level != 0)
                return Reject(ConfigurationStatus::InvalidQosLevel, i);
            // API 23 / firmware 6.1.0.31, client SHA 9b431d...e6087:
            // CREATED BYTES has SUPPORT_ACK=false; TransSetNeedAck retains
            // the global lock on that branch, for both true AND false.
            // Header/native-probe assertions pin SOFTBUS=1 and NEED_ACK=4.
            // Reject the request before SDK entry, never replace false with
            // omission or invent an SDK rc. Other stages/profiles still need
            // platform qualification; static acceptance is not safety proof.
            if (input.service.profile == Profile::BytesNative && option.level == 1 && option.id == 4 &&
                    (option.stage == OptionStage::SocketBeforeBind || option.stage == OptionStage::SocketBeforeListen))
                return Reject(ConfigurationStatus::UnsafeNeedAckBeforeReady, i);
            const auto inserted = options.emplace(OptionKey{option.stage, option.level, option.id}, i);
            if (!inserted.second && input.options[inserted.first->second].value != option.value)
                return Reject(ConfigurationStatus::ConflictingOption, i, inserted.first->second);
        }

        // Copy all user-owned strings/vectors once. Repeated identical options
        // stay in their requested order; neither values nor stages are changed.
        auto snapshot = std::shared_ptr<const ConfigurationSnapshot>(
            new ConfigurationSnapshot(input, std::move(service_name)));
        return {std::move(snapshot), ConfigurationStatus::ValidStatic, {}, {}};
    } catch (const std::bad_alloc&) {
        return Reject(ConfigurationStatus::AllocationFailure);
    }
}
} // namespace mdds::dsoftbus::v1
