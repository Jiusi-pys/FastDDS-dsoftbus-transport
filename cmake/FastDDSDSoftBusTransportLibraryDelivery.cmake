option(FASTDDS_DSOFTBUS_INSTALL_PACKAGE "Install relocatable CONFIG-only Fast DDS OpenHarmony DSoftBus Transport libraries" OFF)
option(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME "Build the explicit real Socket library entry (not production)" OFF)
option(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_TESTS "Host-only tests of library entry with existing Fake Socket C symbols" OFF)
if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME AND FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_TESTS)
    message(FATAL_ERROR "FASTDDS_DSOFTBUS_LIBRARY_REAL_FAKE_EXCLUSIVE")
endif()
if(FASTDDS_DSOFTBUS_INSTALL_PACKAGE OR FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME OR FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_TESTS)
    if(NOT FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "CONFIG" OR NOT TARGET fastdds_dsoftbus_transport_facade OR NOT TARGET fastdds_dsoftbus_service_record_binding)
        message(FATAL_ERROR "Fast DDS OpenHarmony DSoftBus Transport library delivery needs CONFIG, default facade and service-record binding")
    endif()
endif()
if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME OR FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_TESTS)
    if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS" OR NOT TARGET fastdds_dsoftbus_public_socket_api)
            message(FATAL_ERROR "Real Fast DDS DSoftBus Transport experiment requires OHOS and the declared public Socket DSO")
        endif()
        set(_entry fastdds_dsoftbus_experimental_runtime)
        set(_socket fastdds_dsoftbus_experimental_socket)
    else()
        set(_entry fastdds_dsoftbus_experimental_runtime_test)
        set(_socket fastdds_dsoftbus_experimental_socket_test)
    endif()
    # Compile the exact existing real backend. Fake C functions are linked only by
    # the Host test executable; neither a Fake implementation nor a HostMock is installed.
    add_library(${_socket} STATIC src/PublicSocketBackend.cpp)
    target_include_directories(${_socket} PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
    target_link_libraries(${_socket} PUBLIC fastdds_dsoftbus_service_record_binding)
    add_library(${_entry} STATIC src/ExperimentalRuntime.cpp src/ProviderAdapter.cpp
        src/HostProviderLoop.cpp src/DSoftBusTransport.cpp src/DSoftBusTransportDescriptor.cpp)
    target_compile_definitions(${_entry} PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1)
    target_link_libraries(${_entry} PUBLIC fastdds_dsoftbus_native_send_execution fastdds_dsoftbus_input_channels
        fastdds_dsoftbus_service_record_binding PRIVATE ${_socket})
    set_property(TARGET ${_entry} PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT library-experiment)
    set_property(TARGET ${_entry} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
    set_property(TARGET ${_entry} PROPERTY INTERFACE_FASTDDS_DSOFTBUS_PROVIDER_ADAPTER_VARIANT library-experiment)
    set_property(TARGET ${_entry} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_PROVIDER_ADAPTER_VARIANT)
    if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME)
        target_link_libraries(${_socket} PRIVATE fastdds_dsoftbus_public_socket_api)
    endif()
    if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_TESTS)
        if(CMAKE_CROSSCOMPILING OR NOT BUILD_TESTING)
            message(FATAL_ERROR "Experimental library Fake seam is Host test-only")
        endif()
        add_executable(experimental_runtime_tests tests/experimental_runtime_tests.cpp tests/FakePublicSocketLink.cpp tests/FakeNodeDirectory.cpp)
        target_include_directories(experimental_runtime_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
        target_link_libraries(experimental_runtime_tests PRIVATE ${_entry})
        include("${CMAKE_CURRENT_SOURCE_DIR}/integrations/ros2/RuntimeSources.cmake")
        add_library(fastdds_dsoftbus_startup_diagnostic_host STATIC ${_files})
        target_include_directories(fastdds_dsoftbus_startup_diagnostic_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
        target_compile_definitions(fastdds_dsoftbus_startup_diagnostic_host PRIVATE FASTDDS_DSOFTBUS_STARTUP_OBSERVATION=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1)
        target_link_libraries(fastdds_dsoftbus_startup_diagnostic_host PUBLIC fastdds_dsoftbus_fastdds_binding)
        add_executable(ros2_startup_diagnostics_tests tests/ros2_host/startup_diagnostics_tests.cpp tests/FakePublicSocketLink.cpp tests/FakeNodeDirectory.cpp
            integrations/ros2/target/NativeObservation.cpp probes/PublicSocketRtpsObservation.cpp)
        target_include_directories(ros2_startup_diagnostics_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
        target_link_libraries(ros2_startup_diagnostics_tests PRIVATE fastdds_dsoftbus_startup_diagnostic_host)
        target_compile_definitions(ros2_startup_diagnostics_tests PRIVATE FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST=1)
        if(MSVC)
            target_compile_options(ros2_startup_diagnostics_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(ros2_startup_diagnostics_tests PRIVATE -UNDEBUG)
        endif()
        foreach(_case normal local socket listen gate gate_throw bind match cleanup capacity capacity_reject null_receiver v1_locator v1_locator_bad_text node_api_types)
            add_test(NAME dsoftbus.ros2_startup.${_case} COMMAND ros2_startup_diagnostics_tests ${_case})
            set_tests_properties(dsoftbus.ros2_startup.${_case} PROPERTIES TIMEOUT 20)
        endforeach()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(FASTDDS_DSOFTBUS_OBSERVER_EXIT_WSL_DISTRO "Ubuntu-20.04" CACHE STRING
            "Existing Linux WSL environment for the observer finalization component test")
        add_test(NAME dsoftbus.ros2_startup.exit_lifetime
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/ros2_host/observer_exit_lifetime_tests.py"
                --output-root "${CMAKE_CURRENT_BINARY_DIR}/observer-exit-evidence"
                --wsl-distro "${FASTDDS_DSOFTBUS_OBSERVER_EXIT_WSL_DISTRO}")
        set_tests_properties(dsoftbus.ros2_startup.exit_lifetime PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
        # One Runtime lifecycle per process: the installed entry documents that
        # contract and the process-global service-record table is not reusable
        # across two live pairs, so each case runs as its own process.
        add_test(NAME dsoftbus.library.experimental_assembly COMMAND experimental_runtime_tests)
        add_test(NAME dsoftbus.library.v1_identity COMMAND experimental_runtime_tests --identity)
        add_test(NAME dsoftbus.library.reaccept_real_rejected COMMAND experimental_runtime_tests --reaccept-real-rejected)
        add_test(NAME dsoftbus.library.v1_service_scope COMMAND experimental_runtime_tests --service-scope)
        add_test(NAME dsoftbus.library.v1_service_reject COMMAND experimental_runtime_tests --service-reject)
        add_test(NAME dsoftbus.library.experimental_preconnect_gate COMMAND experimental_runtime_tests --gate)
        add_test(NAME dsoftbus.library.experimental_preconnect_refusal COMMAND experimental_runtime_tests --refusal)
        add_test(NAME dsoftbus.library.experimental_preconnect_ownership COMMAND experimental_runtime_tests --ownership)
        add_test(NAME dsoftbus.library.experimental_preconnect_throw COMMAND experimental_runtime_tests --throw)
        add_test(NAME dsoftbus.library.experimental_preconnect_reentrant_lifetime COMMAND experimental_runtime_tests --reentrant-lifetime)
        set_tests_properties(dsoftbus.library.experimental_assembly
            dsoftbus.library.experimental_preconnect_gate
            dsoftbus.library.experimental_preconnect_refusal
            dsoftbus.library.experimental_preconnect_ownership
            dsoftbus.library.experimental_preconnect_throw
            dsoftbus.library.experimental_preconnect_reentrant_lifetime PROPERTIES TIMEOUT 30)
        add_executable(installed_consumer_identity_tests
            tests/installed_consumer_identity_tests.cpp
            examples/installed_runtime_dual_board/identity/token_first_entry.cpp)
        target_include_directories(installed_consumer_identity_tests PRIVATE examples/installed_runtime_dual_board/identity)
        target_compile_features(installed_consumer_identity_tests PRIVATE cxx_std_17)
        add_test(NAME dsoftbus.library.installed_consumer_identity COMMAND installed_consumer_identity_tests)
        set_tests_properties(dsoftbus.library.installed_consumer_identity PROPERTIES TIMEOUT 15)
    endif()
endif()
if(FASTDDS_DSOFTBUS_INSTALL_PACKAGE)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(_core fastdds_dsoftbus_fastdds_binding fastdds_dsoftbus_directory fastdds_dsoftbus_native_owner
        fastdds_dsoftbus_send_queue fastdds_dsoftbus_input_channels fastdds_dsoftbus_application_attachments
        fastdds_dsoftbus_runtime_send_path fastdds_dsoftbus_native_send_execution fastdds_dsoftbus_process
        fastdds_dsoftbus_service_record_table fastdds_dsoftbus_service_record_binding fastdds_dsoftbus_transport_facade)
    foreach(_target IN LISTS _core)
        if(NOT TARGET ${_target})
            message(FATAL_ERROR "Fast DDS OpenHarmony DSoftBus Transport delivery missing required core ${_target}")
        endif()
    endforeach()
    add_library(fastdds_dsoftbus_example_types STATIC
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSamplePubSubTypes.cxx
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSampleTypeObjectSupport.cxx)
    target_include_directories(fastdds_dsoftbus_example_types PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/experiments/fastdds_socket/keyed/type>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus/examples>)
    target_link_libraries(fastdds_dsoftbus_example_types PUBLIC fastdds_dsoftbus_fastdds_binding)
    list(APPEND _core fastdds_dsoftbus_example_types)
    # Remove source/output paths from __FILE__ and COFF diagnostic records only.
    # This does not alter generated serialization or runtime ownership logic.
    foreach(_mapped IN LISTS _core)
        if(_mapped STREQUAL "fastdds_dsoftbus_fastdds_binding")
            continue()
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _fastdds_dsoftbus_native_source)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" _fastdds_dsoftbus_native_build)
            target_compile_options(${_mapped} PRIVATE /experimental:deterministic
                "/pathmap:${_fastdds_dsoftbus_native_source}=." "/pathmap:${_fastdds_dsoftbus_native_build}=.build")
        elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            target_compile_options(${_mapped} PRIVATE "/clang:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "/clang:-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.build")
        else()
            target_compile_options(${_mapped} PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.build")
        endif()
    endforeach()
    install(TARGETS ${_core} EXPORT FastDDSDSoftBusTransportTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # Concrete facade layout and injection/probe access are private variant details.
    install(DIRECTORY include/fastdds_dsoftbus/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus
        FILES_MATCHING PATTERN "*.hpp" PATTERN "DSoftBusTransport.hpp" EXCLUDE
        PATTERN "Host*.hpp" EXCLUDE PATTERN "RuntimeProbeAccess.hpp" EXCLUDE PATTERN "ProviderAdapter.hpp" EXCLUDE
        PATTERN "ExperimentalRuntime.hpp" EXCLUDE)
    install(DIRECTORY experiments/fastdds_socket/keyed/type/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus/examples
        FILES_MATCHING PATTERN "*.hpp")
    install(EXPORT FastDDSDSoftBusTransportTargets NAMESPACE FastDDSDSoftBusTransport:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusTransport)
    set(FASTDDS_DSOFTBUS_HAS_EXPERIMENTAL FALSE)
    if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME)
        set(FASTDDS_DSOFTBUS_HAS_EXPERIMENTAL TRUE)
        install(TARGETS fastdds_dsoftbus_experimental_runtime fastdds_dsoftbus_experimental_socket EXPORT FastDDSDSoftBusTransportExperimentalTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(FILES include/fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fastdds_dsoftbus/dsoftbus)
        install(EXPORT FastDDSDSoftBusTransportExperimentalTargets NAMESPACE FastDDSDSoftBusTransport:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusTransport)
    endif()
    configure_package_config_file(cmake/FastDDSDSoftBusTransportConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusTransport)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportConfigVersion.cmake" VERSION 0.1.0 COMPATIBILITY ExactVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/FastDDSDSoftBusTransportConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FastDDSDSoftBusTransport)
    install(DIRECTORY examples/installed_consumer DESTINATION ${CMAKE_INSTALL_DATADIR}/FastDDSDSoftBusTransport/examples)
    if(FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME)
        install(DIRECTORY examples/experimental_socket DESTINATION ${CMAKE_INSTALL_DATADIR}/FastDDSDSoftBusTransport/examples)
    endif()

endif()
