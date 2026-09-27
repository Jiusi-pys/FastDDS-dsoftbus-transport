#pragma once
// Extracted read-only observation types from ExperimentalTransport.hpp at6943b6c.
// No ExperimentalTransport dependency or routing is linked.
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
namespace mdds::experiment {
struct FragmentObservation {
    std::array<std::uint8_t,16> writer_guid{};
    std::int32_t sequence_hi = 0;
    std::uint32_t sequence_lo = 0, fragment_start = 0, sample_size = 0;
    std::uint16_t fragment_count = 0, fragment_size = 0;
    std::uint64_t fragment_end = 0;
    std::size_t submessage_offset = 0;
    std::size_t submessage_body_bytes = 0;
    bool inline_qos = false, key_flag = false, payload_bounds_known = false;
    std::uint16_t octets_to_inline_qos = 0;
    // Native message offset and measured remainder after fixed fields/inline
    // QoS; payload_bytes includes up to three RTPS alignment padding bytes.
    std::size_t payload_offset = 0, payload_bytes = 0;
};
struct DataObservation {
    std::array<std::uint8_t,16> writer_guid{};
    std::int32_t sequence_hi=0;
    std::uint32_t sequence_lo=0;
    std::size_t submessage_offset=0,submessage_body_bytes=0;
};
struct ReliabilityControlObservation {
    // Original submessage id: ACKNACK=0x06, HEARTBEAT=0x07,
    // NACK_FRAG=0x12, HEARTBEAT_FRAG=0x13. No inferred peer identity.
    std::uint8_t kind = 0;
    std::array<std::uint8_t,12> source_prefix{}, destination_prefix{};
    bool destination_prefix_known = false;
    std::array<std::uint8_t,4> reader_id{}, writer_id{};
    std::array<std::uint8_t,16> reader_guid{}, writer_guid{};
    bool reader_prefix_known = false, writer_prefix_known = false;
    // ACKNACK bitmap base / HEARTBEAT first / *_FRAG writerSN.
    std::int32_t sequence_hi = 0, last_sequence_hi = 0;
    std::uint32_t sequence_lo = 0, last_sequence_lo = 0;
    std::uint32_t fragment_base = 0, last_fragment = 0, num_bits = 0;
    // Raw RTPS bitmap words; bit i is 1u << (31-(i%32)) in word i/32.
    std::array<std::uint32_t,8> bitmap{};
    std::uint32_t count = 0;
    bool final_flag = false, liveliness_flag = false;
    std::size_t submessage_offset = 0;
};
struct RtpsObservation {
    bool valid = false;
    std::uint64_t spdp = 0, sedp = 0, sedp_publications = 0, sedp_subscriptions = 0, user = 0;
    std::array<std::uint8_t,16> first_writer_guid{};
    // One entry per DATA_FRAG, not one per native message or DDS sample.
    std::vector<FragmentObservation> fragments;
    // Raw identity of every DATA submessage, independent of first_writer_guid.
    // Diagnostic only; never modifies or filters receiver input.
    std::vector<DataObservation> data;
    std::vector<ReliabilityControlObservation> controls;
    std::uint64_t data_submessages = 0; // DATA only, including builtin writers.
    std::uint64_t statistics_submessages = 0;
    std::size_t statistics_body_bytes = 0;
    // Structural observation only. The ordinary observer never filters data.
    bool drop_shape_candidate = false;
};
// Read-only observer of original RTPS bytes; never adds framing or changes data.
RtpsObservation ObserveRtps(const void*, std::size_t) noexcept;

}
