#include <fastdds_dsoftbus/dsoftbus/EndpointDirectory.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fastdds/config.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fastdds/core/policy/ParameterList.hpp>
#include <fastdds/core/policy/ParameterSerializer.hpp>
#include <rtps/messages/CDRMessage.hpp>

namespace rtps = eprosima::fastdds::rtps;
namespace dds = eprosima::fastdds::dds;
using fastdds_dsoftbus::dsoftbus::EndpointId;
using fastdds_dsoftbus::dsoftbus::Locator;
using fastdds_dsoftbus::dsoftbus::LocatorCodec;

#define WIRE_CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

int RunLocatorWireTests(int argc, char** argv)
{
    const auto endpoint = [](std::uint8_t first) {
        EndpointId id{};
        for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint8_t>(i + 17);
        id[0] = first;
        id[7] = 0xff;
        return id;
    };
    const auto encode = [](const EndpointId& id) {
        Locator locator;
        WIRE_CHECK(LocatorCodec::Encode(id, locator));
        return locator;
    };
    const std::array<rtps::Endianness_t, 2> endians{{rtps::LITTLEEND, rtps::BIGEND}};
    std::vector<std::pair<std::string, std::function<void()>>> cases;

    cases.emplace_back("LW-01", [&] {
        const auto id = endpoint(0xfe);
        const auto original = encode(id);
        for (auto endian : endians) {
            rtps::CDRMessage_t message(64);
            WIRE_CHECK(message.buffer != nullptr);
            message.msg_endian = endian;
            // Calls the linked libfastdds implementation, not copied source.
            WIRE_CHECK(rtps::CDRMessage::add_locator(&message, original));
            WIRE_CHECK(message.pos == 24 && message.length == 24);
            const std::array<std::uint8_t, 8> prefix = endian == rtps::LITTLEEND ?
                    std::array<std::uint8_t, 8>{{0, 0, 0, 0x40, 1, 0, 0, 0}} :
                    std::array<std::uint8_t, 8>{{0x40, 0, 0, 0, 0, 0, 0, 1}};
            WIRE_CHECK(std::equal(prefix.begin(), prefix.end(), message.buffer));
            WIRE_CHECK(std::equal(id.begin(), id.end(), message.buffer + 8));
            message.pos = 0;
            Locator decoded;
            WIRE_CHECK(rtps::CDRMessage::read_locator(&message, &decoded));
            WIRE_CHECK(message.pos == 24 && decoded == original);
            EndpointId roundtrip{};
            WIRE_CHECK(LocatorCodec::Decode(decoded, roundtrip) && roundtrip == id);
        }
    });

    cases.emplace_back("LW-02", [&] {
        const auto original = encode(endpoint(0x12));
        const auto sentinel = encode(endpoint(0x34));
        for (auto endian : endians) {
            rtps::CDRMessage_t message(64);
            WIRE_CHECK(message.buffer != nullptr);
            message.msg_endian = endian;
            WIRE_CHECK(rtps::CDRMessage::add_locator(&message, original));
            WIRE_CHECK(message.length == 24);
            for (std::uint32_t length = 0; length < 24; ++length) {
                message.length = length;
                message.pos = 0;
                auto output = sentinel;
                WIRE_CHECK(!rtps::CDRMessage::read_locator(&message, &output));
                WIRE_CHECK(message.pos == 0 && output == sentinel);
            }
        }
    });

    cases.emplace_back("LW-03", [&] {
        const std::array<dds::ParameterId_t, 3> pids{{dds::PID_METATRAFFIC_UNICAST_LOCATOR,
            dds::PID_DEFAULT_UNICAST_LOCATOR, dds::PID_UNICAST_LOCATOR}};
        const std::array<Locator, 3> locators{{encode(endpoint(0x12)),
            encode(endpoint(0x34)), encode(endpoint(0xfe))}};
        for (auto endian : endians) {
            rtps::CDRMessage_t message(128);
            WIRE_CHECK(message.buffer != nullptr);
            message.msg_endian = endian;
            WIRE_CHECK(dds::ParameterList::writeEncapsulationToCDRMsg(&message));
            WIRE_CHECK(message.pos == 4 && message.length == 4);
            WIRE_CHECK(message.buffer[0] == 0 && message.buffer[1] ==
                    (endian == rtps::LITTLEEND ? PL_CDR_LE : PL_CDR_BE));
            WIRE_CHECK(message.buffer[2] == 0 && message.buffer[3] == 0);
            for (std::size_t i = 0; i < pids.size(); ++i) {
                const dds::ParameterLocator_t parameter(pids[i], PARAMETER_LOCATOR_LENGTH, locators[i]);
                WIRE_CHECK(dds::ParameterSerializer<dds::ParameterLocator_t>::add_to_cdr_message(parameter, &message));
                WIRE_CHECK(message.pos == 4 + (i + 1) * 28);
            }
            WIRE_CHECK(dds::ParameterSerializer<dds::Parameter_t>::add_parameter_sentinel(&message));
            WIRE_CHECK(message.pos == 92 && message.length == 92);

            std::vector<dds::ParameterId_t> actual_pids;
            std::vector<Locator> actual_locators;
            const auto processor = [&](rtps::CDRMessage_t* content, const dds::ParameterId_t& pid,
                    std::uint16_t length) {
                if (actual_pids.size() >= pids.size() || length != PARAMETER_LOCATOR_LENGTH) return false;
                dds::ParameterLocator_t parameter(pid, length);
                if (!dds::ParameterSerializer<dds::ParameterLocator_t>::read_from_cdr_message(
                        parameter, content, length)) return false;
                if (content->pos != 24) return false;
                actual_pids.push_back(pid);
                actual_locators.push_back(parameter.locator);
                return true;
            };
            message.pos = 0;
            // The actual ParameterList template must derive endianness from
            // encapsulation, not accidentally retain the writer's setting.
            message.msg_endian = endian == rtps::LITTLEEND ? rtps::BIGEND : rtps::LITTLEEND;
            std::uint32_t qos_size = 0;
            WIRE_CHECK(dds::ParameterList::readParameterListfromCDRMsg(message, processor, true, qos_size));
            WIRE_CHECK(message.msg_endian == endian && message.pos == 92 && qos_size == 88);
            WIRE_CHECK(actual_pids.size() == 3 && actual_locators.size() == 3);
            for (std::size_t i = 0; i < pids.size(); ++i) {
                WIRE_CHECK(actual_pids[i] == pids[i] && actual_locators[i] == locators[i]);
            }

            // A truncated sentinel is failure even after three successful
            // processor callbacks. Partial parsing is never list success.
            actual_pids.clear(); actual_locators.clear();
            message.pos = 0; message.length = 91;
            WIRE_CHECK(!dds::ParameterList::readParameterListfromCDRMsg(message, processor, true, qos_size));
            WIRE_CHECK(actual_pids.size() == 3 && actual_locators.size() == 3);
        }
    });

    cases.emplace_back("LW-04", [&] {
        EndpointId two{}; two.back() = 2;
        for (const auto& id : {two, endpoint(0xfe), endpoint(0)}) {
            const auto locator = encode(id);
            WIRE_CHECK(!rtps::IPLocator::isMulticast(locator));
            WIRE_CHECK(!rtps::IPLocator::isLocal(locator));
        }
        for (int forbidden = 0; forbidden < 3; ++forbidden) {
            EndpointId id{};
            if (forbidden == 1) id.back() = 1;
            if (forbidden == 2) id[0] = 0xff;
            auto output = encode(two);
            WIRE_CHECK(!LocatorCodec::Encode(id, output));
            WIRE_CHECK(output == encode(two));
        }
    });

    if (argc > 2) return 64;
    std::cout << "locator wire characterization FastDDS=" << FASTDDS_VERSION_STR
              << "; actual libfastdds CDR/IP calls plus upstream ParameterList/ParameterSerializer templates\n";
    int executed = 0;
    int failures = 0;
    for (const auto& test : cases) {
        if (argc == 2 && test.first != argv[1]) continue;
        ++executed;
        try { test.second(); std::cout << test.first << " PASS\n"; }
        catch (const std::exception& error) {
            ++failures;
            std::cout << test.first << " FAIL " << error.what() << '\n';
        }
    }
    if (executed == 0) return 64;
    std::cout << "wire cases=" << executed << " failures=" << failures
              << "; no Socket, permission, SPDP, SEDP or DDS-discovery evidence\n";
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    return RunLocatorWireTests(argc, argv);
}
