cmake_minimum_required(VERSION 3.22)
project(mdds_directory_public_consumer LANGUAGES C CXX)

# Deliberately no CMAKE_CXX_STANDARD or FASTDDS_* definitions here. A normal
# add_subdirectory consumer must inherit the library's public requirements.
get_filename_component(MDDS_CONSUMER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "Consumer regression only" FORCE)
set(BUILD_INTERFACE_PROBE OFF CACHE BOOL "Consumer regression only" FORCE)
set(BUILD_SOFTBUS_CAPABILITY_PROBE OFF CACHE BOOL "Consumer regression only" FORCE)
set(BUILD_NATIVE_PERMISSION_PROBE OFF CACHE BOOL "Consumer regression only" FORCE)
set(BUILD_DSOFTBUS_DIRECTORY ON CACHE BOOL "Consumer regression only" FORCE)
add_subdirectory("${MDDS_CONSUMER_ROOT}" directory)

find_package(Threads REQUIRED)
add_executable(directory_public_consumer "${MDDS_CONSUMER_ROOT}/tests/endpoint_directory_tests.cpp")
target_link_libraries(directory_public_consumer PRIVATE mdds_dsoftbus_directory Threads::Threads)
if(MSVC)
    target_compile_options(directory_public_consumer PRIVATE /EHsc)
endif()
enable_testing()
add_test(NAME dsoftbus.directory_public_consumer COMMAND directory_public_consumer)
set_tests_properties(dsoftbus.directory_public_consumer PROPERTIES TIMEOUT 15)
