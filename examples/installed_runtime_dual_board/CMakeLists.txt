cmake_minimum_required(VERSION 3.22)
project(mdds_installed_runtime_consumer LANGUAGES CXX)

# The consumer resolves the already-installed MDDS component only. MDDS_DIR must
# point at an installed <prefix>/lib/cmake/MDDS so a moved or stale prefix cannot
# win a search. Nothing here adds the MDDS source tree as a subdirectory.
find_package(MDDS CONFIG REQUIRED COMPONENTS Experimental)

if(NOT IS_ABSOLUTE "${MDDS_TOKEN_BRIDGE}" OR NOT EXISTS "${MDDS_TOKEN_BRIDGE}")
    message(FATAL_ERROR "The identity bridge must be an explicit existing platform C/POD library")
endif()
add_library(mdds_identity_bridge SHARED IMPORTED)
set_target_properties(mdds_identity_bridge PROPERTIES IMPORTED_LOCATION "${MDDS_TOKEN_BRIDGE}")

add_executable(mdds_installed_consumer
    main.cpp
    identity/token_first_entry.cpp)
target_include_directories(mdds_installed_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mdds_installed_consumer PRIVATE
    MDDS::mdds_experimental_runtime
    MDDS::mdds_example_types
    mdds_identity_bridge)
target_compile_features(mdds_installed_consumer PRIVATE cxx_std_17)

# Target loader paths are deployment inputs. Never embed the build host's DSO paths.
set_target_properties(mdds_installed_consumer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN")
