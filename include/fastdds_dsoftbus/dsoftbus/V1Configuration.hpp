#pragma once

#include <fastdds_dsoftbus/dsoftbus/V1Address.hpp>
#include <chrono>
#include <memory>

namespace fastdds_dsoftbus::dsoftbus::v1 {

enum class TxMode { Queue, Native };
enum class RxMode { Queue, Inline };
enum class ConfigurationIntent { Probe, Production };
enum class OptionStage { ListenQos, BindQos, SocketBeforeListen, SocketBeforeBind, SocketAfterReady };

// Numeric keys are already resolved SDK aliases. QoS stages require level=0:
// native QosTV has only qos/value, whereas socket options have a level.
// Unknown/reserved keys and opaque values are retained, not marshalled to an
// SDK type. QosTV int32 conversion and native effects require later validation.
struct OptionRequest
{
    OptionStage stage = OptionStage::BindQos;
    std::int32_t level = 0;
    std::int32_t id = 0;
    std::vector<std::uint8_t> value;
};

struct ResourceBudget
{
    // Requested concurrent bounds. Static validation does not reserve process
    // capacity or attest that the SDK can sustain these values.
    std::uint64_t messages = 64;
    std::uint64_t bytes = 4 * 1024 * 1024;
    std::uint64_t connections = 16;
    std::uint64_t workers = 2;
};

struct OperationLimits
{
    std::chrono::milliseconds connect{5000};
    std::chrono::milliseconds queue{1000};
    std::chrono::milliseconds native_send{1000};
    std::chrono::milliseconds close{1000};
};

struct ConfigurationInput
{
    ServiceAddress service;
    ConfigurationIntent intent = ConfigurationIntent::Probe;
    std::string qualification_profile;
    // Probe request defaults only, never production qualification evidence.
    std::uint64_t max_message_size = 60000;
    // Deployment slots; duplicates are rejected, and order is preserved.
    std::vector<std::uint32_t> initial_peer_slots;
    TxMode tx_mode = TxMode::Queue;
    RxMode rx_mode = RxMode::Queue;
    ResourceBudget participant_budget;
    ResourceBudget process_budget{256, 16 * 1024 * 1024, 64, 8};
    OperationLimits operations;
    std::vector<OptionRequest> options;
};

enum class ConfigurationStatus
{
    ValidStatic, InvalidService, InvalidIntent, InvalidQualificationReference,
    MissingQualificationReference, InvalidMode, InvalidMessageSize,
    InvalidBudget, MessageExceedsBudget, ParticipantExceedsProcess,
    InvalidOperationLimit, InvalidPeerSlot, DuplicatePeerSlot,
    InvalidOptionStage, InvalidQosLevel, ConflictingOption, AllocationFailure,
    UnsafeNeedAckBeforeReady
};
enum class ConfigurationReadiness { NeedsPlatformQualification };

class ConfigurationSnapshot final
{
public:
    const ConfigurationInput& input() const noexcept { return input_; }
    const std::string& service_name() const noexcept { return service_name_; }
    ConfigurationReadiness readiness() const noexcept
    { return ConfigurationReadiness::NeedsPlatformQualification; }
    bool native_ready() const noexcept { return false; }
private:
    friend class ConfigurationValidator;
    ConfigurationSnapshot(const ConfigurationInput&, std::string);
    ConfigurationInput input_;
    std::string service_name_;
};

struct ConfigurationResult
{
    std::shared_ptr<const ConfigurationSnapshot> snapshot;
    ConfigurationStatus status = ConfigurationStatus::InvalidService;
    // Offending input index for peer/options failures, otherwise absent.
    std::optional<std::size_t> index;
    std::optional<std::size_t> conflicting_index;
    explicit operator bool() const noexcept
    { return status == ConfigurationStatus::ValidStatic && bool(snapshot); }
};

class ConfigurationValidator final
{
public:
    // Static validation, including the pinned target's known unsafe NEED_ACK
    // before Bind/Listen on BYTES. Durations must be positive and not milliseconds::max().
    // They are requested budgets, NOT proven native bounds; future deadline
    // arithmetic must separately check overflow against its actual clock.
    // No SDK call, capability lookup, file loading, or production registration.
    static ConfigurationResult Validate(const ConfigurationInput& input);
};

} // namespace fastdds_dsoftbus::dsoftbus::v1
