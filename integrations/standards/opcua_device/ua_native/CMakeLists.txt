cmake_minimum_required(VERSION 3.22)
project(D5UaNative LANGUAGES C CXX)
if(NOT CMAKE_CROSSCOMPILING OR NOT OHOS_ARCH STREQUAL "arm64-v8a" OR NOT EXISTS "${D5_UA_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "D5 UA requires explicit native toolchain and pinned source")
endif()
add_subdirectory("${D5_UA_SOURCE}" upstream)
# SDK 23 defines __linux__, so upstream ua_config_default.c references this
# provider. Upstream's CMake_SYSTEM_NAME==Linux check omits it for OHOS. Compile
# the unchanged upstream provider alongside its other POSIX plugins.
target_sources(open62541-plugins PRIVATE "${D5_UA_SOURCE}/arch/eventloop_posix_eth.c")
