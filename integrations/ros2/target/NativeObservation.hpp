#pragma once
#include <string>
#include "../StartupDiagnostics.hpp"
namespace fastdds_dsoftbus::ros2_target_observation {
void Begin(const std::string&, unsigned);
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void SetSourceForHostInjection() noexcept;
#endif
bool Capture(ros2_diagnostics::Snapshot&) noexcept;
bool ExportPending(ros2_diagnostics::Writer=ros2_diagnostics::Stdout,void* context=nullptr) noexcept; // Control/pump context only; never callback I/O.
bool Flush(); // Control thread only, after local close; not SDK drain proof.
}
