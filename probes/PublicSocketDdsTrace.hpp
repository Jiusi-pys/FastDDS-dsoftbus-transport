#pragma once
#include <cstddef>
#include <cstdint>
namespace mdds::dsoftbus::dds_experiment {
// Observation only, never consulted by any route/admission/protocol decision.
// Borrow ends at return. Called only after the shim's buffer validation.
void SocketFrame(const char*,std::int32_t,const std::uint8_t*,std::size_t) noexcept;
}
