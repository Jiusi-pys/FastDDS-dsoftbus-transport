#if defined(_WIN32)
#define PLATFORM_TEST_EXPORT __declspec(dllexport)
#else
#define PLATFORM_TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PLATFORM_TEST_EXPORT unsigned PlatformTestAbi() { return 21405; }
#include "../../integrations/ros2/StartupDiagnostics.hpp"
namespace {void (*barrier)()=nullptr;}
extern "C" PLATFORM_TEST_EXPORT void PlatformTestCopyBarrier(void(*value)()){barrier=value;}
extern "C" PLATFORM_TEST_EXPORT int PlatformTestCapture(fastdds_dsoftbus::ros2_diagnostics::Snapshot* output){
    if(barrier)barrier();*output={};output->initialized=true;return 0;
}
