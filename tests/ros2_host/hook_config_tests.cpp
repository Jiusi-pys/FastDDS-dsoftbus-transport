#include "../../integrations/ros2/FastddsHook.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    assert(argc == 4);
    const std::string library = argv[1], file = argv[2], ending = std::string(argv[3]) == "crlf" ? "\r\n" : "\n";
    { std::ofstream out(file, std::ios::binary); out << "bridge_library=" << library << ending; }
#if defined(_WIN32)
    _putenv_s("FASTDDS_DSOFTBUS_REQUIRED_CONFIG", file.c_str());
#else
    setenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG", file.c_str(), 1);
#endif
    bool admitted = false;
    auto lease = fastdds_dsoftbus_hook::prepare(201, nullptr, admitted);
    assert(admitted && lease && lease->owner && lease->module);
    assert(fastdds_dsoftbus_hook::release_pending(lease));
    assert(lease->close_state == 2 && !lease->owner);
    std::cout << "HOOK_CONFIG_PASS actual_prepare=1 ending=" << argv[3] << '\n';
}
