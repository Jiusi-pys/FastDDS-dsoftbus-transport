#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include "StartupDiagnostics.hpp"
// A link-time import binds ordinary application consumption to the actual
// patched 2.14 DLL. This performs no Runtime/Socket/thread work in static init.
#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned FastddsDsoftbusFactoryHookAbi();
extern "C" __declspec(dllimport) int FastddsDsoftbusFactoryCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot*) noexcept;
#else
extern "C" unsigned FastddsDsoftbusFactoryHookAbi();
extern "C" int FastddsDsoftbusFactoryCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot*) noexcept;
#endif
namespace {
struct AbiCheck {
    AbiCheck(){if(FastddsDsoftbusFactoryHookAbi()!=21405){std::fputs("FDDST_FACTORY_ABI_MISMATCH\n",stderr);std::abort();}}
} abi_check;
}
extern "C" void FastddsDsoftbusFailureSnapshot(std::uint64_t failure_ref) noexcept {
    fastdds_dsoftbus::ros2_diagnostics::Snapshot snapshot;
    if(FastddsDsoftbusFactoryCaptureStartup(&snapshot)!=0){std::fputs("ROS_DIAGNOSTIC_CAPTURE status=INCOMPLETE reason=NO_SAFE_MODULE_SNAPSHOT\n",stderr);return;}
    if(!fastdds_dsoftbus::ros2_diagnostics::Write(snapshot,fastdds_dsoftbus::ros2_diagnostics::Stdout,nullptr,failure_ref))std::fputs("ROS_DIAGNOSTIC_CAPTURE status=INCOMPLETE reason=OUTPUT_FAILURE\n",stderr);
    std::fflush(stdout);
}
