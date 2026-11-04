#include "../../integrations/ros2/HostProfileGuard.hpp"
#include <cassert>
#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    assert(argc == 4);
    const auto input = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto expected = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const bool accept = std::string(argv[3]) == "accept";
    const auto module = LoadLibraryW(input.c_str());
    assert(module);
    bool protected_work_entered = false;
    std::string error;
    try
    {
        fastdds_dsoftbus::ros2_host_profile::ValidateHistoricalFactory(module, expected, 1);
        protected_work_entered = true;
    }
    catch (const std::exception& failure)
    {
        error = failure.what();
    }
    assert(protected_work_entered == accept);
    assert(accept ? error.empty() : !error.empty());
    if (!accept) assert(error.find(argv[3]) != std::string::npos);
    assert(FreeLibrary(module));
    std::printf("HOST_PROFILE_GUARD source=SYNTHETIC_EXPORT_FIXTURE accepted=%u reason=%s\n",
            unsigned(protected_work_entered), error.c_str());
}
