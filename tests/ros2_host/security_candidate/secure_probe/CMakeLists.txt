cmake_minimum_required(VERSION 3.22)
project(Cve63340SecurityProbe LANGUAGES CXX)

# Host-only probes for CVE-2026-63340 verification on the explicit SECURITY=ON
# Fast DDS 2.14.7 / Fast CDR 2.2.8 candidate. Nothing here is installed and
# nothing here is part of a production default; the default candidate build
# remains SECURITY=OFF.
#
# The caller must point fastrtps_DIR at the isolated SECURITY=ON prefix and
# fastcdr_DIR at the matching Fast CDR prefix built for the same CRT.

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fastrtps 2.14.7 EXACT CONFIG REQUIRED)
find_package(fastcdr 2.2.8 EXACT CONFIG REQUIRED)
find_package(foonathan_memory CONFIG REQUIRED)

add_executable(secure_discovery_probe secure_discovery_probe.cpp)
target_link_libraries(secure_discovery_probe PRIVATE fastrtps fastcdr foonathan_memory)

# Direct observation of where the receive-path allocation decision is taken.
add_executable(bounded_payload_pool_tests bounded_payload_pool_tests.cpp)
target_link_libraries(bounded_payload_pool_tests PRIVATE fastrtps fastcdr foonathan_memory)

# Fragment-geometry evidence with real, non-zero fragment sizes.
add_executable(fragment_geometry_tests fragment_geometry_tests.cpp)
target_link_libraries(fragment_geometry_tests PRIVATE fastrtps fastcdr foonathan_memory)

if(MSVC)
    target_compile_options(secure_discovery_probe PRIVATE /W4 /EHsc)
    target_compile_options(bounded_payload_pool_tests PRIVATE /W4 /EHsc)
    target_compile_options(fragment_geometry_tests PRIVATE /W4 /EHsc)
endif()

# Copy the runtime DLLs next to the probe so the process never picks up an
# uninstrumented or differently configured fastrtps from PATH.
if(WIN32)
    foreach(_target secure_discovery_probe bounded_payload_pool_tests fragment_geometry_tests)
        foreach(_dep fastrtps fastcdr)
            if(TARGET ${_dep})
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${_dep}>
                        $<TARGET_FILE_DIR:${_target}>)
            endif()
        endforeach()
    endforeach()
endif()
