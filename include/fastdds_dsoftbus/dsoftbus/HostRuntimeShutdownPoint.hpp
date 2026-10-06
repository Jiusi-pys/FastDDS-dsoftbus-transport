#pragma once
#include <cstddef>
namespace fastdds_dsoftbus::dsoftbus::host_test {
// Private assembly POD. Local settlement only, never SDK drain evidence.
struct RuntimeShutdownPoint {
    bool stopping=false,sealed=false;
    std::size_t active_pumps=0;
};
}
