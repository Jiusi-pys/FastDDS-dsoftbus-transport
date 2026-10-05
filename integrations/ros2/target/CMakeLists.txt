cmake_minimum_required(VERSION 3.22)
project(FastDDSDSoftBusTransportRos2Target LANGUAGES CXX)
include(RosTargetBuildOptions.cmake)
option(FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT "Explicit finite normal-close/fresh-ID experiment; default real reaccept remains refused" OFF)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS" OR NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "This experiment requires the actual OHOS cross toolchain")
endif()
if(NOT OHOS_STL STREQUAL "c++_shared")
    message(FATAL_ERROR "Factory/Runtime C++ object ABI requires the declared shared SDK runtime")
endif()
set(CMAKE_CXX_STANDARD 17)
find_package(fastrtps 2.14.5 EXACT CONFIG REQUIRED)
find_package(fastcdr 2 CONFIG REQUIRED)
# This pinned upstream release does not ship a ConfigVersion.cmake. The real
# header assertion and recorded source/ELF identity bind its version instead.
find_package(tinyxml2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
set(DSOFTBUS_SOURCE_DIR "" CACHE PATH "Explicit authorized Socket source headers")
set(FASTDDS_DSOFTBUS_SOCKET_LIBRARY "" CACHE FILEPATH "Declared real platform public Socket DSO, never packaged replacement")
if(NOT IS_ABSOLUTE "${DSOFTBUS_SOURCE_DIR}" OR NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h"
        OR NOT IS_ABSOLUTE "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}" OR NOT EXISTS "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}")
    message(FATAL_ERROR "Explicit public source headers and declared platform DSO required")
endif()
include(../RuntimeSources.cmake)
add_library(fastdds_dsoftbus_ros2_214_target_runtime STATIC ${_files})
target_include_directories(fastdds_dsoftbus_ros2_214_target_runtime PUBLIC
    "$<BUILD_INTERFACE:${_transport_root}/include>" "$<INSTALL_INTERFACE:include>"
    PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
target_compile_definitions(fastdds_dsoftbus_ros2_214_target_runtime PUBLIC FASTDDS_DSOFTBUS_FASTDDS_214=1
    PRIVATE FASTDDS_DSOFTBUS_STARTUP_OBSERVATION=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1)
target_link_libraries(fastdds_dsoftbus_ros2_214_target_runtime PUBLIC fastrtps fastcdr Threads::Threads)
add_library(fastdds_dsoftbus_ros2_target_bridge SHARED TargetBridge.cpp NativeObservation.cpp
    "${_transport_root}/probes/PublicSocketRtpsObservation.cpp")
target_compile_definitions(fastdds_dsoftbus_ros2_target_bridge PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
if(FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT)
    target_compile_definitions(fastdds_dsoftbus_ros2_214_target_runtime PRIVATE FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT=1)
    target_compile_definitions(fastdds_dsoftbus_ros2_target_bridge PRIVATE FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT=1)
endif()
target_link_libraries(fastdds_dsoftbus_ros2_target_bridge PRIVATE fastdds_dsoftbus_ros2_214_target_runtime
    "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}" tinyxml2 dl)
add_library(factory_configuration INTERFACE)
target_link_libraries(factory_configuration INTERFACE fastrtps)
foreach(_target IN ITEMS fastdds_dsoftbus_ros2_214_target_runtime factory_configuration)
    set_property(TARGET ${_target} PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT ros2-214-ohos-experiment)
    set_property(TARGET ${_target} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
endforeach()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS fastdds_dsoftbus_ros2_214_target_runtime fastdds_dsoftbus_ros2_target_bridge factory_configuration
    EXPORT Ros2TargetTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT Ros2TargetTargets NAMESPACE FastDDSDSoftBusTransportRos2Target::
    DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Target)
install(FILES ../FactoryAbiProbe.cpp DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(FILES ../StartupDiagnostics.hpp ../TeardownDiagnostics.hpp "${_transport_root}/examples/ros2_transport/FailureProtocol.hpp" DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(FILES ../ResidualDiagnostics.hpp DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(FILES ../NativePrefix.hpp DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(FILES IdentityStartup.cpp DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(DIRECTORY identity DESTINATION share/FastDDSDSoftBusTransportRos2Target)
install(DIRECTORY "${_transport_root}/include/fastdds_dsoftbus/" DESTINATION include/fastdds_dsoftbus
    FILES_MATCHING PATTERN "*.hpp" PATTERN "DSoftBusTransport.hpp" EXCLUDE PATTERN "Host*.hpp" EXCLUDE
    PATTERN "ProviderAdapter.hpp" EXCLUDE PATTERN "RuntimeProbeAccess.hpp" EXCLUDE)
configure_package_config_file(Ros2TargetConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportRos2TargetConfig.cmake"
    INSTALL_DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Target)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportRos2TargetConfig.cmake"
    DESTINATION lib/cmake/FastDDSDSoftBusTransportRos2Target)
install(DIRECTORY "${_transport_root}/examples/ros2_transport" DESTINATION share/FastDDSDSoftBusTransportRos2Target/examples)
install(DIRECTORY "${_transport_root}/examples/dds214_layer_baseline" DESTINATION share/FastDDSDSoftBusTransportRos2Target/examples COMPONENT LayerBaselineExamples)
