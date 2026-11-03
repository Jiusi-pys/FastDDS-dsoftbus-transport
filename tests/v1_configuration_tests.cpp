#include <fastdds_dsoftbus/dsoftbus/V1Configuration.hpp>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace v1 = fastdds_dsoftbus::dsoftbus::v1;
using namespace std::chrono_literals;
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #c); } while(false)

v1::ConfigurationInput Valid()
{
    v1::ConfigurationInput input;
    input.service = {"mdds", 17, 3, v1::Profile::BytesNative};
    input.initial_peer_slots = {5, 0, v1::AddressCodec::max_slot};
    return input;
}

void Rejected(const v1::ConfigurationInput& input, v1::ConfigurationStatus status)
{
    auto result = v1::ConfigurationValidator::Validate(input);
    CHECK(!result); CHECK(!result.snapshot); CHECK(result.status == status);
}

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> cases{
        {"CFG-01-owned-immutable-snapshot", [] {
            auto input = Valid(); input.qualification_profile = "unverified-record-reference";
            input.options.push_back({v1::OptionStage::SocketBeforeBind, 42, -9, {0, 255, 1}});
            const auto result = v1::ConfigurationValidator::Validate(input); CHECK(result);
            const auto original_name = result.snapshot->service_name();
            input.service.name_space = "changed"; input.service.slot = 10;
            input.initial_peer_slots[0] = 9; input.qualification_profile.clear(); input.options[0].value[0] = 8;
            CHECK(result.snapshot->input().service.name_space == "mdds");
            CHECK(result.snapshot->input().service.slot == 3);
            CHECK(result.snapshot->input().initial_peer_slots[0] == 5);
            CHECK(result.snapshot->input().qualification_profile == "unverified-record-reference");
            CHECK(result.snapshot->input().options[0].value == std::vector<std::uint8_t>({0, 255, 1}));
            CHECK(result.snapshot->service_name() == original_name);
            static_assert(std::is_same_v<decltype(result.snapshot->input()), const v1::ConfigurationInput&>);
        }},
        {"CFG-02-valid-static-never-native-ready", [] {
            for (const auto profile : {v1::Profile::BytesNative, v1::Profile::RawPlain, v1::Profile::RawEncrypted}) {
                for (const auto tx : {v1::TxMode::Queue, v1::TxMode::Native}) {
                    for (const auto rx : {v1::RxMode::Queue, v1::RxMode::Inline}) {
                        auto input = Valid(); input.service.profile = profile; input.tx_mode = tx; input.rx_mode = rx;
                        auto result = v1::ConfigurationValidator::Validate(input); CHECK(result);
                        CHECK(!result.snapshot->native_ready());
                        CHECK(result.snapshot->readiness() == v1::ConfigurationReadiness::NeedsPlatformQualification);
                        CHECK(result.snapshot->input().tx_mode == tx && result.snapshot->input().rx_mode == rx);
                    }
                }
            }
        }},
        {"CFG-03-production-reference-is-not-qualification", [] {
            auto input = Valid(); input.intent = v1::ConfigurationIntent::Production;
            Rejected(input, v1::ConfigurationStatus::MissingQualificationReference);
            input.qualification_profile = "partial-or-missing-record";
            auto result = v1::ConfigurationValidator::Validate(input); CHECK(result); CHECK(!result.snapshot->native_ready());
            input.qualification_profile = std::string("record\0suffix", 13);
            Rejected(input, v1::ConfigurationStatus::InvalidQualificationReference);
            input = Valid(); input.intent = static_cast<v1::ConfigurationIntent>(99);
            Rejected(input, v1::ConfigurationStatus::InvalidIntent);
        }},
        {"CFG-04-address-and-profile-boundaries", [] {
            auto input = Valid(); input.service.domain = 0xffffffff; input.service.slot = v1::AddressCodec::max_slot;
            CHECK(v1::ConfigurationValidator::Validate(input));
            input.service.slot++; Rejected(input, v1::ConfigurationStatus::InvalidService);
            input = Valid(); input.service.profile = static_cast<v1::Profile>(4);
            Rejected(input, v1::ConfigurationStatus::InvalidService);
            input = Valid(); input.service.name_space.clear(); Rejected(input, v1::ConfigurationStatus::InvalidService);
        }},
        {"CFG-05-peer-slots-explicit-order-and-duplicates", [] {
            auto input = Valid(); auto result = v1::ConfigurationValidator::Validate(input); CHECK(result);
            CHECK(result.snapshot->input().initial_peer_slots == input.initial_peer_slots);
            input.initial_peer_slots.push_back(5); result = v1::ConfigurationValidator::Validate(input);
            CHECK(result.status == v1::ConfigurationStatus::DuplicatePeerSlot && result.index == 3 && result.conflicting_index == 0);
            input.initial_peer_slots = {v1::AddressCodec::max_slot + 1}; Rejected(input, v1::ConfigurationStatus::InvalidPeerSlot);
            input.initial_peer_slots.clear(); CHECK(v1::ConfigurationValidator::Validate(input));
        }},
        {"CFG-06-budget-counters-and-componentwise-process", [] {
            std::uint64_t v1::ResourceBudget::* const fields[] = {&v1::ResourceBudget::messages, &v1::ResourceBudget::bytes,
                &v1::ResourceBudget::connections, &v1::ResourceBudget::workers};
            for (const auto field : fields) {
                auto input = Valid(); input.participant_budget.*field = 0; Rejected(input, v1::ConfigurationStatus::InvalidBudget);
                input = Valid(); input.process_budget.*field = 0; Rejected(input, v1::ConfigurationStatus::InvalidBudget);
                input = Valid(); input.participant_budget.*field = input.process_budget.*field + 1;
                Rejected(input, v1::ConfigurationStatus::ParticipantExceedsProcess);
            }
            auto input = Valid(); input.process_budget = input.participant_budget; CHECK(v1::ConfigurationValidator::Validate(input));
            input.participant_budget.bytes = input.max_message_size - 1; Rejected(input, v1::ConfigurationStatus::MessageExceedsBudget);
        }},
        {"CFG-07-message-boundaries-no-size-truncation", [] {
            auto input = Valid(); input.max_message_size = 0; Rejected(input, v1::ConfigurationStatus::InvalidMessageSize);
            input = Valid(); input.max_message_size = std::uint64_t{1} << 32;
            Rejected(input, v1::ConfigurationStatus::InvalidMessageSize);
            input.max_message_size = std::numeric_limits<std::uint64_t>::max();
            Rejected(input, v1::ConfigurationStatus::InvalidMessageSize);
            input = Valid(); input.max_message_size = std::numeric_limits<std::uint32_t>::max();
            input.participant_budget.bytes = input.process_budget.bytes = input.max_message_size;
            CHECK(v1::ConfigurationValidator::Validate(input));
        }},
        {"CFG-08-positive-finite-operation-requests", [] {
            std::chrono::milliseconds v1::OperationLimits::* const fields[] = {&v1::OperationLimits::connect,
                &v1::OperationLimits::queue, &v1::OperationLimits::native_send, &v1::OperationLimits::close};
            for (const auto field : fields) {
                for (const auto duration : {0ms, -1ms, std::chrono::milliseconds::max()}) {
                    auto input = Valid(); input.operations.*field = duration;
                    Rejected(input, v1::ConfigurationStatus::InvalidOperationLimit);
                }
                auto input = Valid(); input.operations.*field = 1ms; CHECK(v1::ConfigurationValidator::Validate(input));
            }
        }},
        {"CFG-09-unknown-options-preserve-order-bytes-and-stage", [] {
            auto input = Valid();
            for (const auto stage : {v1::OptionStage::ListenQos, v1::OptionStage::BindQos, v1::OptionStage::SocketBeforeListen,
                    v1::OptionStage::SocketBeforeBind, v1::OptionStage::SocketAfterReady})
                input.options.push_back({stage,
                    stage == v1::OptionStage::ListenQos || stage == v1::OptionStage::BindQos ? 0 : -7,
                    std::numeric_limits<std::int32_t>::max(), {0, 1, 0, 255}});
            input.options.push_back({v1::OptionStage::SocketAfterReady, 77, -20, {}});
            const auto result = v1::ConfigurationValidator::Validate(input); CHECK(result);
            CHECK(result.snapshot->input().options.size() == input.options.size());
            for (std::size_t i = 0; i < input.options.size(); ++i) {
                const auto& option = result.snapshot->input().options[i];
                CHECK(option.stage == input.options[i].stage && option.level == input.options[i].level);
                CHECK(option.id == input.options[i].id && option.value == input.options[i].value);
            }
        }},
        {"CFG-10-resolved-numeric-alias-conflict", [] {
            auto input = Valid(); input.options = {{v1::OptionStage::BindQos, 0, 12, {1}}, {v1::OptionStage::BindQos, 0, 12, {1}}};
            auto result = v1::ConfigurationValidator::Validate(input); CHECK(result); CHECK(result.snapshot->input().options.size() == 2);
            input.options[1].value = {2}; result = v1::ConfigurationValidator::Validate(input);
            CHECK(result.status == v1::ConfigurationStatus::ConflictingOption && result.index == 1 && result.conflicting_index == 0);
            input.options[1].stage = v1::OptionStage::ListenQos; CHECK(v1::ConfigurationValidator::Validate(input));
            input.options[0].stage = input.options[1].stage = v1::OptionStage::SocketAfterReady;
            input.options[1].level = 1; CHECK(v1::ConfigurationValidator::Validate(input));
            input.options[1].stage = static_cast<v1::OptionStage>(90); Rejected(input, v1::ConfigurationStatus::InvalidOptionStage);
        }},
        {"CFG-11-invalid-modes-not-defaulted", [] {
            auto input = Valid(); CHECK(input.tx_mode == v1::TxMode::Queue && input.rx_mode == v1::RxMode::Queue);
            input.tx_mode = static_cast<v1::TxMode>(9); Rejected(input, v1::ConfigurationStatus::InvalidMode);
            input = Valid(); input.rx_mode = static_cast<v1::RxMode>(9); Rejected(input, v1::ConfigurationStatus::InvalidMode);
        }},
        {"CFG-12-qos-has-no-level-discriminator", [] {
            // Public trans_type.h QosTV is {qos, int32_t value}; a fake level
            // cannot distinguish contradictory requests for the same QoS ID.
            for (const auto stage : {v1::OptionStage::ListenQos, v1::OptionStage::BindQos}) {
                for (const auto level : {1, -1, std::numeric_limits<std::int32_t>::min(),
                        std::numeric_limits<std::int32_t>::max()}) {
                    auto input = Valid(); input.options = {{stage, 0, 12, {1}}, {stage, level, 12, {2}}};
                    const auto result = v1::ConfigurationValidator::Validate(input);
                    CHECK(!result && !result.snapshot);
                    CHECK(result.status == v1::ConfigurationStatus::InvalidQosLevel && result.index == 1);
                    input.options = {{stage, level, -99, {}}};
                    const auto single = v1::ConfigurationValidator::Validate(input);
                    CHECK(single.status == v1::ConfigurationStatus::InvalidQosLevel && single.index == 0);
                }
                auto input = Valid(); input.options = {{stage, 0, 12, {1}}, {stage, 0, 12, {2}}};
                const auto result = v1::ConfigurationValidator::Validate(input);
                CHECK(result.status == v1::ConfigurationStatus::ConflictingOption && result.index == 1 && result.conflicting_index == 0);
                input.options[1].value = input.options[0].value;
                CHECK(v1::ConfigurationValidator::Validate(input));
            }
        }},
        {"CFG-13-reject-created-bytes-need-ack-before-sdk", [] {
            // Pinned target: client SHA 9b431d...e6087, API 23, firmware
            // 6.1.0.31. Both setter values enter the unsupported lock-retention
            // branch before Bind/Listen. False is NOT omission of the setter.
            for (const auto stage : {v1::OptionStage::SocketBeforeBind, v1::OptionStage::SocketBeforeListen}) {
                for (const std::uint8_t requested : {std::uint8_t{0}, std::uint8_t{1}}) {
                    auto input = Valid(); input.options = {{stage, 1, 4, {requested}}};
                    const auto result = v1::ConfigurationValidator::Validate(input);
                    CHECK(!result && !result.snapshot && result.index == 0);
                    CHECK(result.status == v1::ConfigurationStatus::UnsafeNeedAckBeforeReady);
                    CHECK(input.options[0].value == std::vector<std::uint8_t>{requested});
                    // Static validation has no SDK import/call. There is no SDK
                    // rc field to fabricate; this is an adapter rejection.
                    std::cout << "option=NEED_ACK requested=" << unsigned(requested)
                              << " decision=REJECTED sdk=NotCalled\n";
                }
            }
        }},
        {"CFG-14-need-ack-omission-and-other-scopes-not-qualified", [] {
            auto input = Valid();
            CHECK(v1::ConfigurationValidator::Validate(input));
            for (const auto stage : {v1::OptionStage::BindQos, v1::OptionStage::ListenQos,
                    v1::OptionStage::SocketAfterReady}) {
                input.options = {{stage, stage == v1::OptionStage::SocketAfterReady ? 1 : 0, 4, {0}}};
                const auto result = v1::ConfigurationValidator::Validate(input);
                CHECK(result && !result.snapshot->native_ready());
                CHECK(result.snapshot->input().options[0].value == std::vector<std::uint8_t>{0});
            }
            input.options = {{v1::OptionStage::SocketBeforeBind, 7, 4, {0}}};
            CHECK(v1::ConfigurationValidator::Validate(input));
            input.options = {{v1::OptionStage::SocketBeforeBind, 1, 4, {1}}};
            for (const auto profile : {v1::Profile::RawPlain, v1::Profile::RawEncrypted}) {
                input.service.profile = profile;
                const auto result = v1::ConfigurationValidator::Validate(input);
                CHECK(result && !result.snapshot->native_ready());
            }
        }}
    };
    int failed = 0;
    for (const auto& test : cases) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    std::cout << "Configuration tests: " << cases.size() - failed << '/' << cases.size() << "; native_ready=false\n";
    return failed ? 1 : 0;
}
