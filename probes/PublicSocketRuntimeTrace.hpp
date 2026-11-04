#pragma once
#include <cstdint>
#include <cstddef>
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
// Fixed-size observation storage; no payload pointers retained, no I/O in capture.
void TraceEvent(const char*,std::uint64_t slot,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept;
void TraceText(const char*,const char*) noexcept;
std::size_t TraceCount() noexcept;
std::size_t TraceOverflow() noexcept;
bool TraceHas(const char*) noexcept;
void DumpTrace() noexcept;
// Probe observation scope only; default zero preserves unsampled old probes.
// Stage-local: never an SDK event generation or admission authority.
void SetObservationCycle(unsigned) noexcept;
unsigned ObservationCycle() noexcept;
bool DetailedObservation() noexcept;
}
