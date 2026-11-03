# Include after project() in an isolated unchanged ROS package build.
# OHOS musl provides XSI strerror_r (int), not the glibc GNU char* variant.
# Select the existing POSIX branch for this one translation unit; keep all
# other upstream flags and implementations untouched. No platform macro spoof.
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    # Host CMake has no OHOS platform module; the SDK still supplies the actual
    # target compiler/sysroot. Explicit ELF SONAME support prevents absolute
    # build-machine paths from entering downstream DT_NEEDED entries.
    set(CMAKE_SHARED_LIBRARY_SONAME_C_FLAG "-Wl,-soname,")
    set(CMAKE_SHARED_LIBRARY_SONAME_CXX_FLAG "-Wl,-soname,")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS" AND PROJECT_NAME STREQUAL "rcutils")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/strerror.c" APPEND
        PROPERTY COMPILE_OPTIONS "-U_GNU_SOURCE")
endif()
