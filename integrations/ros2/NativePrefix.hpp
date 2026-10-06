#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace fastdds_dsoftbus { namespace ros2_diagnostics {
constexpr std::size_t prefix_capacity=128;
struct PrefixFrame {std::uint64_t observation=0;bool sending=false;std::int32_t socket=0;
    std::array<std::uint8_t,16> writer{};std::int32_t high=0;std::uint32_t low=0;};
struct NativePrefix {
    char run[17]{};unsigned side=0;bool valid=false;
    std::uint64_t send_enter=0,send_return=0,send_error=0,ingress=0,overflow=0,malformed=0;
    std::size_t total_stored=0,first_index=0,stored=0;
    PrefixFrame frames[prefix_capacity]{};
};
}}
