cmake_minimum_required(VERSION 3.22)
project(FastDDSDSoftBusTransportRos2Host LANGUAGES CXX)
if(NOT WIN32 OR CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "The opaque-byte seam is Windows Host-only, never device admission")
endif()
if(MSVC AND DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
    message(FATAL_ERROR "ROS Host profile requires the Release DLL CRT /MD")
endif()
set(CMAKE_CXX_STANDARD 17)
find_package(fastrtps 2.14.5 EXACT CONFIG REQUIRED)
find_package(fastcdr 2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
set(DSOFTBUS_SOURCE_DIR "" CACHE PATH "Explicit authorized public Socket source headers")
if(NOT IS_ABSOLUTE "${DSOFTBUS_SOURCE_DIR}" OR NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h")
    message(FATAL_ERROR "An explicit existing public Socket source header tree is required")
endif()
get_filename_component(_transport_root "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(_runtime_sources EndpointDirectory V1Address V1Configuration NativeOwnerTable
    ServiceRecordTable ServiceRecordBinding SendQueue InputChannels ApplicationAttachments
    RuntimeSendPath DSoftBusSenderResource NativeSendExecution ProcessReceivePath ProcessCoordinator
    DSoftBusTransport DSoftBusTransportDescriptor HostProviderLoop ProviderAdapter
    PublicSocketBackend ExperimentalRuntime)
set(_files)
foreach(_source IN LISTS _runtime_sources)
    list(APPEND _files "${_transport_root}/src/${_source}.cpp")
endforeach()
add_library(fastdds_dsoftbus_ros2_214_runtime STATIC ${_files})
target_include_directories(fastdds_dsoftbus_ros2_214_runtime PUBLIC
    "$<BUILD_INTERFACE:${_transport_root}/include>" "$<INSTALL_INTERFACE:include>")
target_include_directories(fastdds_dsoftbus_ros2_214_runtime PRIVATE
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
target_compile_definitions(fastdds_dsoftbus_ros2_214_runtime PUBLIC FASTDDS_DSOFTBUS_FASTDDS_214=1
    PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1
        FASTDDS_DSOFTBUS_PUBLIC_SOCKET_HOST_C_SEAM=1)
target_link_libraries(fastdds_dsoftbus_ros2_214_runtime PUBLIC fastrtps fastcdr Threads::Threads)
set_property(TARGET fastdds_dsoftbus_ros2_214_runtime PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT ros2-214-host-experiment)
set_property(TARGET fastdds_dsoftbus_ros2_214_runtime APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
add_library(fastdds_dsoftbus_ros2_host_bridge SHARED Bridge.cpp HostSocket.cpp Observation.cpp)
target_link_libraries(fastdds_dsoftbus_ros2_host_bridge PRIVATE fastdds_dsoftbus_ros2_214_runtime ws2_32)
target_include_directories(fastdds_dsoftbus_ros2_host_bridge PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
target_compile_features(fastdds_dsoftbus_ros2_host_bridge PRIVATE cxx_std_17)
include(CTest)
if(BUILD_TESTING)
    add_executable(ros2_hook_lifecycle_tests "${_transport_root}/tests/ros2_host/hook_lifecycle_tests.cpp")
    # Assertions remain execution checks in the Release-only ROS ABI profile.
    target_compile_options(ros2_hook_lifecycle_tests PRIVATE /UNDEBUG)
    add_test(NAME ros2_hook_lifecycle COMMAND ros2_hook_lifecycle_tests)
endif()
add_library(factory_configuration INTERFACE)
target_link_libraries(factory_configuration INTERFACE fastrtps)
set_property(TARGET factory_configuration PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT ros2-214-host-experiment)
set_property(TARGET factory_configuration APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS fastdds_dsoftbus_ros2_214_runtime fastdds_dsoftbus_ros2_host_bridge factory_configuration
    EXPORT Ros2HostTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${_transport_root}/include/fastdds_dsoftbus/" DESTINATION include/fastdds_dsoftbus
    FILES_MATCHING PATTERN "*.hpp" PATTERN "DSoftBusTransport.hpp" EXCLUDE PATTERN "Host*.hpp" EXCLUDE
    PATTERN "ProviderAdapter.hpp" EXCLUDE PATTERN "RuntimeProbeAccess.hpp" EXCLUDE)
install(FILES FactoryAbiProbe.cpp DESTINATION share/FastDDSDSoftBusTransportRos2Host)
install(DIRECTORY "${_transport_root}/examples/ros2_transport" DESTINATION share/FastDDSDSoftBusTransportRos2Host/examples)
install(EXPORT Ros2HostTargets NAMESPACE FastDDSDSoftBusTransportRos2Host:: DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Host)
configure_package_config_file(Ros2HostConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportRos2HostConfig.cmake"
    INSTALL_DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Host)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportRos2HostConfig.cmake" DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Host)
