cmake_minimum_required(VERSION 3.22)
option(BUILD_STOCK_214_LOWER_REGRESSIONS "Build original lower-Transport regressions against exact stock Fast DDS 2.14.5 (Host only)" OFF)
if(BUILD_STOCK_214_LOWER_REGRESSIONS AND DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
    message(FATAL_ERROR "BUILD_STOCK_214_LOWER_REGRESSIONS requires BUILD_TESTING=ON")
endif()
option(BUILD_STOCK_214_HOST_LOOP_REGRESSIONS "Build original HostLoop/input-close regressions against exact stock Fast DDS 2.14.5 (Host only)" OFF)
if(BUILD_STOCK_214_HOST_LOOP_REGRESSIONS AND DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
    message(FATAL_ERROR "BUILD_STOCK_214_HOST_LOOP_REGRESSIONS requires BUILD_TESTING=ON")
endif()
option(BUILD_STOCK_214_SHARED_REGRESSIONS "Build original shared-channel regressions against exact stock Fast DDS 2.14.5 (Host only)" OFF)
if(BUILD_STOCK_214_SHARED_REGRESSIONS AND DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
    message(FATAL_ERROR "BUILD_STOCK_214_SHARED_REGRESSIONS requires BUILD_TESTING=ON")
endif()
project(StockFastddsTransportBootstrap LANGUAGES CXX)
find_package(fastrtps 2.14.5 EXACT CONFIG REQUIRED)
find_package(fastcdr 2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(stock_transport_bootstrap STATIC TransportBootstrap.cpp)
target_include_directories(stock_transport_bootstrap PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(stock_transport_bootstrap PUBLIC cxx_std_17)
target_link_libraries(stock_transport_bootstrap PUBLIC fastrtps)
include(GNUInstallDirs)
install(TARGETS stock_transport_bootstrap ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES TransportBootstrap.hpp ScopedTransportBootstrap.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus/stock_transport)

include(CTest)
if(BUILD_STOCK_214_LOWER_REGRESSIONS AND CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "BUILD_STOCK_214_LOWER_REGRESSIONS is a Host-only regression entry")
endif()
if(BUILD_STOCK_214_HOST_LOOP_REGRESSIONS AND CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "BUILD_STOCK_214_HOST_LOOP_REGRESSIONS is a Host-only regression entry")
endif()
if(BUILD_STOCK_214_SHARED_REGRESSIONS AND CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "BUILD_STOCK_214_SHARED_REGRESSIONS is a Host-only regression entry")
endif()
set(DSOFTBUS_SOURCE_DIR "" CACHE PATH "Read-only official Socket headers")
option(BUILD_STOCK_TRANSPORT_RUNTIME_COMPILE_ONLY "Compile the Transport runtime against stock OHOS headers; no link/runtime claim" OFF)
if(BUILD_STOCK_TRANSPORT_RUNTIME_COMPILE_ONLY)
    if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        message(FATAL_ERROR "Runtime compile qualification requires the explicit OHOS toolchain")
    endif()
    if(NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h")
        message(FATAL_ERROR "Explicit official DSoftBus Socket headers required")
    endif()
    include(../RuntimeSources.cmake)
    add_library(stock_transport_runtime_compile STATIC ${_files} "${_repo}/integrations/ros2/Observation.cpp")
    target_compile_features(stock_transport_runtime_compile PRIVATE cxx_std_17)
    target_compile_definitions(stock_transport_runtime_compile PRIVATE FASTDDS_DSOFTBUS_FASTDDS_214=1
        FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    target_include_directories(stock_transport_runtime_compile PRIVATE "${_repo}/include"
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center"
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
    target_link_libraries(stock_transport_runtime_compile PRIVATE fastrtps fastcdr Threads::Threads)
endif()

# Reusable current-source OHOS product. This does not enable the legacy bridge.
option(BUILD_STOCK_TRANSPORT_OHOS_PRODUCT "Build/install the opt-in real-Socket stock OHOS product and metadata-only loader" OFF)
if(BUILD_STOCK_TRANSPORT_OHOS_PRODUCT)
    if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        message(FATAL_ERROR "Stock product requires the explicit OHOS cross toolchain")
    endif()
    if(NOT OHOS_STL STREQUAL "c++_shared")
        message(FATAL_ERROR "Stock product requires the declared shared SDK C++ runtime")
    endif()
    find_package(fastcdr 2.2.5 EXACT CONFIG REQUIRED)
    if(NOT IS_ABSOLUTE "${DSOFTBUS_SOURCE_DIR}" OR NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h")
        message(FATAL_ERROR "Stock product requires explicit official Socket source headers")
    endif()
    set(FASTDDS_DSOFTBUS_SOCKET_LIBRARY "" CACHE FILEPATH "Manifest-bound real platform libsoftbus_client.z.so; never a stub/replacement")
    set(FASTDDS_DSOFTBUS_SOCKET_SHA256 "" CACHE STRING "Expected SHA256 of the declared real Socket provider")
    set(FASTDDS_DSOFTBUS_PLATFORM_LINK_DIRECTORIES "" CACHE STRING "Explicit real-provider cache directories; link-time rpath-link only")
    if(NOT IS_ABSOLUTE "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}" OR NOT EXISTS "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}")
        message(FATAL_ERROR "Stock product requires an explicit real Socket provider file")
    endif()
    string(LENGTH "${FASTDDS_DSOFTBUS_SOCKET_SHA256}" _socket_hash_length)
    if(NOT _socket_hash_length EQUAL 64 OR NOT FASTDDS_DSOFTBUS_SOCKET_SHA256 MATCHES "^[0-9A-Fa-f]+$")
        message(FATAL_ERROR "Stock product requires the provider manifest SHA256")
    endif()
    file(SHA256 "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}" _socket_actual_hash)
    string(TOLOWER "${FASTDDS_DSOFTBUS_SOCKET_SHA256}" _socket_expected_hash)
    if(NOT _socket_actual_hash STREQUAL _socket_expected_hash)
        message(FATAL_ERROR "Declared Socket provider SHA256 mismatch")
    endif()
    if(NOT IS_ABSOLUTE "${CMAKE_READELF}" OR NOT EXISTS "${CMAKE_READELF}")
        message(FATAL_ERROR "Stock product requires the SDK readelf tool for provider qualification")
    endif()
    execute_process(COMMAND "${CMAKE_READELF}" -h -d "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}"
        RESULT_VARIABLE _socket_elf_result OUTPUT_VARIABLE _socket_elf ERROR_VARIABLE _socket_elf_error)
    if(NOT _socket_elf_result EQUAL 0 OR NOT _socket_elf MATCHES "Machine:[ \t]+AArch64"
            OR NOT _socket_elf MATCHES "\\(SONAME\\)[^\n]*\\[libsoftbus_client\\.z\\.so\\]")
        message(FATAL_ERROR "Declared Socket provider must be AArch64 ELF with SONAME libsoftbus_client.z.so: ${_socket_elf_error}")
    endif()
    if(NOT FASTDDS_DSOFTBUS_PLATFORM_LINK_DIRECTORIES)
        message(FATAL_ERROR "Explicit real-provider rpath-link directories are required")
    endif()
    foreach(_provider_dir IN LISTS FASTDDS_DSOFTBUS_PLATFORM_LINK_DIRECTORIES)
        if(NOT IS_ABSOLUTE "${_provider_dir}" OR NOT IS_DIRECTORY "${_provider_dir}")
            message(FATAL_ERROR "Invalid explicit platform link directory: ${_provider_dir}")
        endif()
    endforeach()
    include(../RuntimeSources.cmake)
    add_library(stock_dsoftbus_platform_client SHARED IMPORTED)
    set_target_properties(stock_dsoftbus_platform_client PROPERTIES
        IMPORTED_LOCATION "${FASTDDS_DSOFTBUS_SOCKET_LIBRARY}" IMPORTED_SONAME "libsoftbus_client.z.so")
    # 20 current runtime sources + current Observation + current Bootstrap = 22 TU.
    add_library(fastdds_dsoftbus_stock_transport SHARED ${_files}
        "${_repo}/integrations/ros2/Observation.cpp" TransportBootstrap.cpp)
    add_library(FastDDSDSoftBusStock::Transport ALIAS fastdds_dsoftbus_stock_transport)
    set_target_properties(fastdds_dsoftbus_stock_transport PROPERTIES
        EXPORT_NAME Transport OUTPUT_NAME fastdds_dsoftbus_stock_214
        CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN" INSTALL_RPATH_USE_LINK_PATH FALSE)
    target_compile_features(fastdds_dsoftbus_stock_transport PUBLIC cxx_std_17)
    target_compile_definitions(fastdds_dsoftbus_stock_transport PUBLIC
        FASTDDS_DSOFTBUS_FASTDDS_214=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1
        PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    target_include_directories(fastdds_dsoftbus_stock_transport PUBLIC
        "$<BUILD_INTERFACE:${_repo}/include>" "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus/stock_transport>"
        PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
    target_link_libraries(fastdds_dsoftbus_stock_transport PUBLIC fastrtps fastcdr
        PRIVATE Threads::Threads stock_dsoftbus_platform_client)
    target_link_options(fastdds_dsoftbus_stock_transport PRIVATE
        "LINKER:-soname,libfastdds_dsoftbus_stock_214.so"
        "LINKER:--enable-new-dtags" "LINKER:-rpath,$ORIGIN"
        "LINKER:-z,defs" "LINKER:--no-undefined" "LINKER:--no-allow-shlib-undefined")
    foreach(_provider_dir IN LISTS FASTDDS_DSOFTBUS_PLATFORM_LINK_DIRECTORIES)
        target_link_options(fastdds_dsoftbus_stock_transport PRIVATE "LINKER:-rpath-link,${_provider_dir}")
    endforeach()
    set_property(TARGET fastdds_dsoftbus_stock_transport PROPERTY
        INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT stock-214-ohos-product)
    set_property(TARGET fastdds_dsoftbus_stock_transport APPEND PROPERTY
        COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
    enable_language(C)
    add_executable(stock_transport_loader StockTransportLoader.c)
    # Source remains C; use the declared C++ consumer's shared-SDK link driver.
    # This models its ABI ownership, not arbitrary C-host unload safety.
    # Verify the actual root libc++_shared dependency in the linked ELF.
    set_target_properties(stock_transport_loader PROPERTIES LINKER_LANGUAGE CXX)
    target_compile_features(stock_transport_loader PRIVATE c_std_11)
    target_compile_definitions(stock_transport_loader PRIVATE _GNU_SOURCE=1)
    target_link_libraries(stock_transport_loader PRIVATE ${CMAKE_DL_LIBS})
    include(CMakePackageConfigHelpers)
    configure_package_config_file(StockTransportConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusStockConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusStock")
    install(TARGETS fastdds_dsoftbus_stock_transport EXPORT FastDDSDSoftBusStockTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(TARGETS stock_transport_loader RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT FastDDSDSoftBusStockTargets NAMESPACE FastDDSDSoftBusStock::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusStock")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusStockConfig.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusStock")
    install(DIRECTORY "${_repo}/include/fastdds_dsoftbus/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus"
        FILES_MATCHING PATTERN "*.hpp" PATTERN "RuntimeProbeAccess.hpp" EXCLUDE)
    install(DIRECTORY consumer DESTINATION "${CMAKE_INSTALL_DATADIR}/FastDDSDSoftBusStock"
        FILES_MATCHING PATTERN "CMakeLists.txt" PATTERN "*.cpp")
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    if(NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h")
        message(FATAL_ERROR "Explicit official DSoftBus Socket source headers required")
    endif()
    include(../RuntimeSources.cmake)
    add_library(stock_transport_host_fake STATIC ${_files}
        "${_repo}/tests/FakePublicSocketLink.cpp" "${_repo}/tests/FakeNodeDirectory.cpp"
        "${_repo}/integrations/ros2/Observation.cpp")
    target_compile_features(stock_transport_host_fake PUBLIC cxx_std_17)
    target_compile_definitions(stock_transport_host_fake PUBLIC
        FASTDDS_DSOFTBUS_FASTDDS_214=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1
        FASTDDS_DSOFTBUS_PUBLIC_SOCKET_HOST_C_SEAM=1
        PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    target_include_directories(stock_transport_host_fake PUBLIC "${_repo}/include" "${_repo}/tests"
        PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
    target_link_libraries(stock_transport_host_fake PUBLIC fastrtps fastcdr Threads::Threads)
    if(BUILD_STOCK_214_LOWER_REGRESSIONS)
        include("${CMAKE_CURRENT_LIST_DIR}/Stock214LowerRegressions.cmake")
    endif()
    if(BUILD_STOCK_214_HOST_LOOP_REGRESSIONS OR BUILD_STOCK_214_SHARED_REGRESSIONS)
        include("${CMAKE_CURRENT_LIST_DIR}/Stock214HostLoopRegressions.cmake")
    endif()
    if(BUILD_STOCK_214_SHARED_REGRESSIONS)
        include("${CMAKE_CURRENT_LIST_DIR}/Stock214SharedRegressions.cmake")
    endif()
    add_executable(stock_transport_bootstrap_tests
        "${_repo}/tests/ros2_host/stock_transport/bootstrap_tests.cpp")
    target_link_libraries(stock_transport_bootstrap_tests PRIVATE
        stock_transport_bootstrap stock_transport_host_fake)
    foreach(_case lifecycle defaults-changed invalid)
        add_test(NAME stock_transport.${_case} COMMAND stock_transport_bootstrap_tests ${_case})
        set_tests_properties(stock_transport.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_domain_tests
        "${_repo}/tests/ros2_host/stock_transport/domain_scope_tests.cpp")
    target_link_libraries(stock_transport_domain_tests PRIVATE stock_transport_bootstrap stock_transport_host_fake)
    target_compile_definitions(stock_transport_domain_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case core transport positive bootstrap empty-peers unsupported-peer input-reopen input-early-cycles input-reentry input-publication)
        add_test(NAME stock_transport.domain.${_case} COMMAND stock_transport_domain_tests ${_case})
        set_tests_properties(stock_transport.domain.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_work_tests
        "${_repo}/tests/ros2_host/stock_transport/runtime_work_tests.cpp")
    target_link_libraries(stock_transport_work_tests PRIVATE
        stock_transport_bootstrap stock_transport_host_fake)
    target_compile_definitions(stock_transport_work_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case signal pair factory stop stop-inflight factory-held-native unsupported)
        add_test(NAME stock_transport.work.${_case} COMMAND stock_transport_work_tests ${_case})
        set_tests_properties(stock_transport.work.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_default_domain_tests
        "${_repo}/tests/ros2_host/stock_transport/default_domain_tests.cpp")
    target_link_libraries(stock_transport_default_domain_tests PRIVATE stock_transport_bootstrap stock_transport_host_fake)
    target_compile_definitions(stock_transport_default_domain_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case assembly allocation raw mismatch151 positive152 mismatch152 empty-peers unsupported-peer startup-failure startup-close)
        add_test(NAME stock_transport.default_domain.${_case} COMMAND stock_transport_default_domain_tests ${_case})
        set_tests_properties(stock_transport.default_domain.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_process_receive_tests "${_repo}/tests/process_receive_tests.cpp")
    target_link_libraries(stock_process_receive_tests PRIVATE stock_transport_host_fake)
    add_test(NAME stock_transport.process_receive COMMAND stock_process_receive_tests)
    set_tests_properties(stock_transport.process_receive PROPERTIES TIMEOUT 25)
    add_executable(stock_transport_views_tests
        "${_repo}/tests/ros2_host/stock_transport/runtime_views_tests.cpp")
    target_link_libraries(stock_transport_views_tests PRIVATE stock_transport_host_fake)
    target_compile_definitions(stock_transport_views_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case factory repeat domains borrowed closed-callback held-receiver self-match-diagnostic
            udp-fixed-cross-domain udp-mapped-same-domain udp-mapped-cross-domain)
        add_test(NAME stock_transport.views.${_case} COMMAND stock_transport_views_tests ${_case})
        set_tests_properties(stock_transport.views.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    # Additive diagnostic controls only; old default/domain oracles remain intact.
    add_executable(stock_transport_domain_normal_tests
        "${_repo}/tests/ros2_host/stock_transport/domain_normal_controls_tests.cpp")
    target_link_libraries(stock_transport_domain_normal_tests PRIVATE stock_transport_host_fake)
    target_compile_definitions(stock_transport_domain_normal_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case mixed151-152 mixed152-153 native-bound await-senders expected-zero)
        add_test(NAME stock_transport.normal_control.${_case} COMMAND stock_transport_domain_normal_tests ${_case})
        set_tests_properties(stock_transport.normal_control.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_domain_circuit_tests
        "${_repo}/tests/ros2_host/stock_transport/domain_circuit_tests.cpp")
    target_link_libraries(stock_transport_domain_circuit_tests PRIVATE stock_transport_host_fake)
    target_compile_definitions(stock_transport_domain_circuit_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    target_include_directories(stock_transport_domain_circuit_tests PRIVATE
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
    foreach(_case names default-remote identity socket-collision raw)
        add_test(NAME stock_transport.circuit.${_case} COMMAND stock_transport_domain_circuit_tests ${_case})
        set_tests_properties(stock_transport.circuit.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_multi_participant_tests
        "${_repo}/tests/ros2_host/stock_transport/multi_participant_tests.cpp")
    target_link_libraries(stock_transport_multi_participant_tests PRIVATE stock_transport_host_fake)
    target_compile_definitions(stock_transport_multi_participant_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case cross-close allocation per-view-domain)
        add_test(NAME stock_transport.multi.${_case} COMMAND stock_transport_multi_participant_tests ${_case})
        set_tests_properties(stock_transport.multi.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    add_executable(stock_transport_default_participant_tests
        "${_repo}/tests/ros2_host/stock_transport/default_participant_tests.cpp")
    target_link_libraries(stock_transport_default_participant_tests PRIVATE stock_transport_host_fake)
    target_compile_definitions(stock_transport_default_participant_tests PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
    foreach(_case borrowed allocation cross-close pending-refund unique-stop observer-throw observer-reentry start-failure)
        add_test(NAME stock_transport.default.${_case} COMMAND stock_transport_default_participant_tests ${_case})
        set_tests_properties(stock_transport.default.${_case} PROPERTIES TIMEOUT 25)
    endforeach()
    # Consistent observer ABI in its own small variant; existing targets retain
    # their original compile definitions and observation layout.
    get_target_property(_default_sources stock_transport_host_fake SOURCES)
    get_target_property(_default_includes stock_transport_host_fake INCLUDE_DIRECTORIES)
    get_target_property(_default_definitions stock_transport_host_fake COMPILE_DEFINITIONS)
    add_library(stock_transport_default_observed_fake STATIC ${_default_sources})
    target_compile_features(stock_transport_default_observed_fake PUBLIC cxx_std_17)
    target_include_directories(stock_transport_default_observed_fake PUBLIC ${_default_includes})
    target_compile_definitions(stock_transport_default_observed_fake PUBLIC ${_default_definitions} FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION=1)
    target_link_libraries(stock_transport_default_observed_fake PUBLIC fastrtps fastcdr Threads::Threads)
    add_executable(stock_transport_default_observed_tests
        "${_repo}/tests/ros2_host/stock_transport/default_participant_tests.cpp")
    target_link_libraries(stock_transport_default_observed_tests PRIVATE stock_transport_default_observed_fake)
    add_test(NAME stock_transport.default.observed COMMAND stock_transport_default_observed_tests observed)
    set_tests_properties(stock_transport.default.observed PROPERTIES TIMEOUT 25)
    add_executable(stock_transport_default_domain_observed_tests
        "${_repo}/tests/ros2_host/stock_transport/default_domain_tests.cpp")
    target_link_libraries(stock_transport_default_domain_observed_tests PRIVATE stock_transport_bootstrap stock_transport_default_observed_fake)
    add_test(NAME stock_transport.default_domain.observed COMMAND stock_transport_default_domain_observed_tests assembly)
    set_tests_properties(stock_transport.default_domain.observed PROPERTIES TIMEOUT 25)
endif()
