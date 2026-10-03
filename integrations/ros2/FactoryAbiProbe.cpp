#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include "StartupDiagnostics.hpp"
#include "TeardownDiagnostics.hpp"
#include "NativePrefix.hpp"
// A link-time import binds ordinary application consumption to the actual
// patched 2.14 DLL. This performs no Runtime/Socket/thread work in static init.
#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned FastddsDsoftbusFactoryHookAbi();
extern "C" __declspec(dllimport) int FastddsDsoftbusFactoryCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot*) noexcept;
extern "C" __declspec(dllimport) int FastddsDsoftbusFactoryCaptureTeardown(fastdds_dsoftbus::ros2_teardown::Snapshot*) noexcept;
extern "C" __declspec(dllimport) void FastddsDsoftbusFactoryTeardownControl(const char*) noexcept;
extern "C" __declspec(dllimport) int FastddsDsoftbusFactoryCaptureNativePrefix(fastdds_dsoftbus::ros2_diagnostics::NativePrefix*) noexcept;
#else
extern "C" unsigned FastddsDsoftbusFactoryHookAbi();
extern "C" int FastddsDsoftbusFactoryCaptureStartup(fastdds_dsoftbus::ros2_diagnostics::Snapshot*) noexcept;
extern "C" int FastddsDsoftbusFactoryCaptureTeardown(fastdds_dsoftbus::ros2_teardown::Snapshot*) noexcept;
extern "C" void FastddsDsoftbusFactoryTeardownControl(const char*) noexcept;
extern "C" int FastddsDsoftbusFactoryCaptureNativePrefix(fastdds_dsoftbus::ros2_diagnostics::NativePrefix*) noexcept;
#endif
namespace {
struct AbiCheck {
    AbiCheck(){if(FastddsDsoftbusFactoryHookAbi()!=21405){std::fputs("FDDST_FACTORY_ABI_MISMATCH\n",stderr);std::abort();}}
} abi_check;
}
extern "C" void FastddsDsoftbusFailureSnapshot(std::uint64_t failure_ref) noexcept {
    FastddsDsoftbusFactoryTeardownControl("APPLICATION_FAILURE_PREFIX");
    fastdds_dsoftbus::ros2_diagnostics::Snapshot snapshot;
    if(FastddsDsoftbusFactoryCaptureStartup(&snapshot)!=0){std::fputs("ROS_DIAGNOSTIC_CAPTURE status=INCOMPLETE reason=NO_SAFE_MODULE_SNAPSHOT\n",stderr);return;}
    if(!fastdds_dsoftbus::ros2_diagnostics::Write(snapshot,fastdds_dsoftbus::ros2_diagnostics::Stdout,nullptr,failure_ref))std::fputs("ROS_DIAGNOSTIC_CAPTURE status=INCOMPLETE reason=OUTPUT_FAILURE\n",stderr);
    std::fflush(stdout);
}
extern "C" void FastddsDsoftbusTeardownCheckpoint(const char* name) noexcept {
    FastddsDsoftbusFactoryTeardownControl(name);
    if(std::strcmp(name,"BEFORE_PHASE_ENTITY_DELETE")&&std::strcmp(name,"BEFORE_NODE_DELETE"))return;
    fastdds_dsoftbus::ros2_diagnostics::NativePrefix prefix;
    if(FastddsDsoftbusFactoryCaptureNativePrefix(&prefix)!=0||!prefix.valid){std::fputs("ROS_NATIVE_PREFIX_CAPTURE status=INCOMPLETE reason=NO_SAFE_VALUE_COPY\n",stderr);return;}
    for(std::size_t i=0;i<prefix.stored;++i){const auto& frame=prefix.frames[i];char guid[33]{};
        for(std::size_t j=0;j<16;++j)std::snprintf(guid+j*2,3,"%02x",frame.writer[j]);
        std::fprintf(stdout,"ROS_NATIVE_PREFIX_DATA run=%s side=%c checkpoint=%s cache_index=%zu observation=%llu direction=%s socket=%d writer=%s sequence_hi=%d sequence_lo=%u scope=BOUNDED_PRE_DELETE_PREFIX_NOT_FINAL\n",prefix.run,prefix.side?'B':'A',name,prefix.first_index+i,static_cast<unsigned long long>(frame.observation),frame.sending?"SDK_SEND":"SDK_INGRESS",frame.socket,guid,frame.high,frame.low);
    }
    std::fprintf(stdout,"ROS_NATIVE_PREFIX_SUMMARY run=%s side=%c checkpoint=%s stored=%zu total_cached=%zu first_index=%zu limit=%zu send_enter=%llu send_return=%llu send_error=%llu ingress=%llu overflow=%llu malformed=%llu final=0 sdk_epoch=NOT_PROVEN\n",prefix.run,prefix.side?'B':'A',name,prefix.stored,prefix.total_stored,prefix.first_index,fastdds_dsoftbus::ros2_diagnostics::prefix_capacity,static_cast<unsigned long long>(prefix.send_enter),static_cast<unsigned long long>(prefix.send_return),static_cast<unsigned long long>(prefix.send_error),static_cast<unsigned long long>(prefix.ingress),static_cast<unsigned long long>(prefix.overflow),static_cast<unsigned long long>(prefix.malformed));std::fflush(stdout);
}
extern "C" int FastddsDsoftbusTeardownResult() noexcept {
    if(!std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG"))return 0;
    fastdds_dsoftbus::ros2_teardown::Snapshot v;
    if(FastddsDsoftbusFactoryCaptureTeardown(&v)!=0||v.overflow||v.output_errors||v.first_present)return 1;
    bool public_return=false,local_close=false;
    for(std::size_t i=0;i<v.count;++i)if(v.events[i].returned){
        if(!std::strcmp(v.events[i].operation,"Factory_delete_participant")&&!std::strcmp(v.events[i].type,"PUBLIC_RETURN_CODE"))public_return=true;
        if(!std::strcmp(v.events[i].operation,"RuntimeClose"))local_close=true;
    }
    return public_return&&local_close?0:1;
}
