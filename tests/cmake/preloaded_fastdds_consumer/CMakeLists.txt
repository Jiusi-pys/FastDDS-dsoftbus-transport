cmake_minimum_required(VERSION 3.22)
project(mdds_preloaded_fastdds_consumer LANGUAGES C CXX)
get_filename_component(FASTDDS_DSOFTBUS_CONSUMER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(FASTDDS_DSOFTBUS_FASTDDS_MODE CONFIG CACHE STRING "Exercise real preloaded imports")
set(BUILD_TESTING OFF CACHE BOOL "External consumer" FORCE)
set(BUILD_INTERFACE_PROBE OFF CACHE BOOL "No separate probe" FORCE)
set(BUILD_SOFTBUS_CAPABILITY_PROBE OFF CACHE BOOL "No Socket provider" FORCE)
set(BUILD_NATIVE_PERMISSION_PROBE OFF CACHE BOOL "No permission provider" FORCE)

# Intentionally preload in an intermediate directory, then consume the project
# targets from its parent. A local import must not silently lose the upstream
# TARGET_EXISTS-dependent DLL usage requirements at this outer consumer.
add_subdirectory(wrapper)
add_executable(preloaded_fastdds_consumer ../real_fastdds_consumer/main.cpp binding_macros.cpp)
target_link_libraries(preloaded_fastdds_consumer PRIVATE
    fastdds_dsoftbus_runtime_send_path fastdds_dsoftbus_input_channels)
enable_testing()
add_test(NAME binding.preloaded_consumer COMMAND preloaded_fastdds_consumer)
set_tests_properties(binding.preloaded_consumer PROPERTIES TIMEOUT 15)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/effective-artifacts.txt" CONTENT
    "fastdds=$<TARGET_FILE:fastdds>\nfastcdr=$<TARGET_FILE:fastcdr>\nfoonathan=$<TARGET_FILE:foonathan_memory>\n")
