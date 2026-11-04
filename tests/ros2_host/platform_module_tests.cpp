#include "../../integrations/ros2/PlatformModule.hpp"
#include <cassert>
#include <iostream>

namespace platform = fastdds_dsoftbus_hook_platform;
int main(int argc, char** argv)
{
    assert(argc == 2);
    assert(platform::absolute_file(argv[1]));
    assert(!platform::absolute_file("relative-library"));
    assert(!platform::absolute_file(""));
    auto module = platform::load(argv[1]);
    assert(module);
    using Abi = unsigned (*)();
    auto abi = reinterpret_cast<Abi>(platform::symbol(module, "PlatformTestAbi"));
    assert(abi && abi() == 21405);
    assert(!platform::symbol(module, "AbsentContractSymbol"));
    assert(platform::unload(module));
    assert(!platform::load("this-does-not-exist-library"));
    assert(!platform::unload(nullptr));
    std::cout << "PLATFORM_MODULE_PASS actual_load_and_symbol=1\n";
}
