cmake_minimum_required(VERSION 3.22)
project(mdds_real_fastdds_consumer LANGUAGES C CXX)

get_filename_component(MDDS_CONSUMER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(MDDS_FASTDDS_MODE CONFIG CACHE STRING "Binding exercised by this fixture")
set(BUILD_TESTING OFF CACHE BOOL "External consumer" FORCE)
set(BUILD_INTERFACE_PROBE ON CACHE BOOL "Exercise real base implementation" FORCE)
set(BUILD_SOFTBUS_CAPABILITY_PROBE OFF CACHE BOOL "No Socket provider" FORCE)
set(BUILD_NATIVE_PERMISSION_PROBE OFF CACHE BOOL "No permission provider" FORCE)
if(BINDING_CONFLICTING_IMPORT)
    find_package(fastdds 3.6.2.0 EXACT CONFIG REQUIRED COMPONENTS shared
        PATHS "${fastdds_DIR}" NO_DEFAULT_PATH)
    # Keep this negative specific to conflicting includes, independently of the
    # supported preloaded-import visibility contract.
    get_property(conflicting_imports DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(conflicting_import IN LISTS conflicting_imports)
        set_property(TARGET "${conflicting_import}" PROPERTY IMPORTED_GLOBAL TRUE)
    endforeach()
    # Negative fixture only: a parent's conflicting import must not be accepted
    # just because the matching package file returns FOUND on its second load.
    set_property(TARGET fastdds PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${MDDS_CONSUMER_ROOT}/include")
endif()
add_subdirectory("${MDDS_CONSUMER_ROOT}" mdds)

# Assertions precede compilation: old CMake silently ignores the requested mode.
if(NOT TARGET mdds_fastdds_binding OR NOT TARGET fastdds)
    message(FATAL_ERROR "BINDING_CONTRACT: CONFIG did not supply the actual imported binding")
endif()
# Upstream's TARGET_EXISTS-based public DLL definition also requires target
# visibility at this external consumer, not just a successful child find_package.
get_target_property(binding_links mdds_fastdds_binding INTERFACE_LINK_LIBRARIES)
if(NOT "fastdds" IN_LIST binding_links)
    message(FATAL_ERROR "BINDING_CONTRACT: no transitive real Fast DDS target")
endif()
foreach(target IN ITEMS mdds_fastdds_binding mdds_dsoftbus_directory
        mdds_dsoftbus_send_queue mdds_dsoftbus_input_channels
        mdds_dsoftbus_runtime_send_path fastdds_interface_probe)
    foreach(property IN ITEMS INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
            COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(value "${target}" "${property}")
        if(value MATCHES "generated|Fast-DDS/include|FASTDDS_STATIC_LINK|fastdds_EXPORTS")
            message(FATAL_ERROR "BINDING_CONTRACT: ${target} ${property} contaminated: ${value}")
        endif()
    endforeach()
endforeach()
get_target_property(probe_sources fastdds_interface_probe SOURCES)
if(probe_sources MATCHES "TransportInterface.cpp")
    message(FATAL_ERROR "BINDING_CONTRACT: duplicate upstream implementation")
endif()

add_executable(real_fastdds_consumer main.cpp)
# Deliberately no manual upstream include directory, link library, standard or ABI macro.
target_link_libraries(real_fastdds_consumer PRIVATE
    mdds_dsoftbus_runtime_send_path mdds_dsoftbus_input_channels)
enable_testing()
add_test(NAME binding.real_consumer COMMAND real_fastdds_consumer)
add_test(NAME binding.real_interface COMMAND fastdds_interface_tests)
set_tests_properties(binding.real_consumer binding.real_interface PROPERTIES TIMEOUT 15)
