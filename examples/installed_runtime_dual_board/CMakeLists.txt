cmake_minimum_required(VERSION 3.22)
project(fastdds_dsoftbus_installed_runtime_consumer LANGUAGES CXX)

# The consumer resolves the already-installed MDDS component only. FASTDDS_DSOFTBUS_DIR must
# point at an installed <prefix>/lib/cmake/FastDDSDSoftBusTransport so a moved or stale prefix cannot
# win a search. Nothing here adds the MDDS source tree as a subdirectory.
find_package(FastDDSDSoftBusTransport CONFIG REQUIRED COMPONENTS Experimental)

if(NOT IS_ABSOLUTE "${FASTDDS_DSOFTBUS_TOKEN_BRIDGE}" OR NOT EXISTS "${FASTDDS_DSOFTBUS_TOKEN_BRIDGE}")
    message(FATAL_ERROR "The identity bridge must be an explicit existing platform C/POD library")
endif()
add_library(dsoftbus_identity_bridge SHARED IMPORTED)
set_target_properties(dsoftbus_identity_bridge PROPERTIES IMPORTED_LOCATION "${FASTDDS_DSOFTBUS_TOKEN_BRIDGE}")

add_executable(fastdds_dsoftbus_installed_consumer
    main.cpp
    identity/token_first_entry.cpp)
target_include_directories(fastdds_dsoftbus_installed_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(fastdds_dsoftbus_installed_consumer PRIVATE
    FastDDSDSoftBusTransport::fastdds_dsoftbus_experimental_runtime
    FastDDSDSoftBusTransport::fastdds_dsoftbus_example_types
    dsoftbus_identity_bridge)
target_compile_features(fastdds_dsoftbus_installed_consumer PRIVATE cxx_std_17)

# Target loader paths are deployment inputs. Never embed the build host's DSO paths.
set_target_properties(fastdds_dsoftbus_installed_consumer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN")
