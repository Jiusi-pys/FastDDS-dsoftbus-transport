cmake_minimum_required(VERSION 3.22)
project(mdds_experimental_socket_consumer LANGUAGES CXX)
find_package(MDDS CONFIG REQUIRED COMPONENTS Experimental)
add_executable(mdds_socket_consumer main.cpp)
target_link_libraries(mdds_socket_consumer PRIVATE MDDS::mdds_experimental_runtime MDDS::mdds_example_types)
target_compile_features(mdds_socket_consumer PRIVATE cxx_std_17)
# Target loader paths are deployment inputs. Never embed the build host's DSO directories.
set_target_properties(mdds_socket_consumer PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
