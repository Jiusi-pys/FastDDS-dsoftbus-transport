option(MDDS_INSTALL_PACKAGE "Install relocatable CONFIG-only MDDS libraries" OFF)
option(MDDS_BUILD_EXPERIMENTAL_RUNTIME "Build the explicit real Socket library entry (not production)" OFF)
option(MDDS_BUILD_EXPERIMENTAL_TESTS "Host-only tests of library entry with existing Fake Socket C symbols" OFF)
if(MDDS_BUILD_EXPERIMENTAL_RUNTIME AND MDDS_BUILD_EXPERIMENTAL_TESTS)
    message(FATAL_ERROR "MDDS_LIBRARY_REAL_FAKE_EXCLUSIVE")
endif()
if(MDDS_INSTALL_PACKAGE OR MDDS_BUILD_EXPERIMENTAL_RUNTIME OR MDDS_BUILD_EXPERIMENTAL_TESTS)
    if(NOT MDDS_FASTDDS_MODE STREQUAL "CONFIG" OR NOT TARGET mdds_dsoftbus_transport_facade OR NOT TARGET mdds_dsoftbus_service_record_binding)
        message(FATAL_ERROR "MDDS library delivery needs CONFIG, default facade and service-record binding")
    endif()
endif()
if(MDDS_BUILD_EXPERIMENTAL_RUNTIME OR MDDS_BUILD_EXPERIMENTAL_TESTS)
    if(MDDS_BUILD_EXPERIMENTAL_RUNTIME)
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS" OR NOT TARGET mdds_public_socket_api)
            message(FATAL_ERROR "Real MDDS experiment requires OHOS and the declared public Socket DSO")
        endif()
        set(_entry mdds_experimental_runtime)
        set(_socket mdds_experimental_socket)
    else()
        set(_entry mdds_experimental_runtime_test)
        set(_socket mdds_experimental_socket_test)
    endif()
    # Compile the exact existing real backend. Fake C functions are linked only by
    # the Host test executable; neither a Fake implementation nor a HostMock is installed.
    add_library(${_socket} STATIC src/PublicSocketBackend.cpp)
    target_include_directories(${_socket} PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(${_socket} PUBLIC mdds_dsoftbus_service_record_binding)
    add_library(${_entry} STATIC src/ExperimentalRuntime.cpp src/ProviderAdapter.cpp
        src/HostProviderLoop.cpp src/DSoftBusTransport.cpp src/DSoftBusTransportDescriptor.cpp)
    target_compile_definitions(${_entry} PRIVATE MDDS_PUBLIC_SOCKET_RUNTIME_PROBE=1 MDDS_PUBLIC_SOCKET_DDS_EXPERIMENT=1)
    target_link_libraries(${_entry} PUBLIC mdds_dsoftbus_native_send_execution mdds_dsoftbus_input_channels
        mdds_dsoftbus_service_record_binding PRIVATE ${_socket})
    set_property(TARGET ${_entry} PROPERTY INTERFACE_MDDS_FACADE_VARIANT library-experiment)
    set_property(TARGET ${_entry} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING MDDS_FACADE_VARIANT)
    set_property(TARGET ${_entry} PROPERTY INTERFACE_MDDS_PROVIDER_ADAPTER_VARIANT library-experiment)
    set_property(TARGET ${_entry} APPEND PROPERTY COMPATIBLE_INTERFACE_STRING MDDS_PROVIDER_ADAPTER_VARIANT)
    if(MDDS_BUILD_EXPERIMENTAL_RUNTIME)
        target_link_libraries(${_socket} PRIVATE mdds_public_socket_api)
    endif()
    if(MDDS_BUILD_EXPERIMENTAL_TESTS)
        if(CMAKE_CROSSCOMPILING OR NOT BUILD_TESTING)
            message(FATAL_ERROR "Experimental library Fake seam is Host test-only")
        endif()
        add_executable(experimental_runtime_tests tests/experimental_runtime_tests.cpp tests/FakePublicSocketLink.cpp)
        target_include_directories(experimental_runtime_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
        target_link_libraries(experimental_runtime_tests PRIVATE ${_entry})
        add_test(NAME dsoftbus.library.experimental_assembly COMMAND experimental_runtime_tests)
        set_tests_properties(dsoftbus.library.experimental_assembly PROPERTIES TIMEOUT 30)
    endif()
endif()
if(MDDS_INSTALL_PACKAGE)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(_core mdds_fastdds_binding mdds_dsoftbus_directory mdds_dsoftbus_native_owner
        mdds_dsoftbus_send_queue mdds_dsoftbus_input_channels mdds_dsoftbus_application_attachments
        mdds_dsoftbus_runtime_send_path mdds_dsoftbus_native_send_execution mdds_dsoftbus_process
        mdds_dsoftbus_service_record_table mdds_dsoftbus_service_record_binding mdds_dsoftbus_transport_facade)
    foreach(_target IN LISTS _core)
        if(NOT TARGET ${_target})
            message(FATAL_ERROR "MDDS delivery missing required core ${_target}")
        endif()
    endforeach()
    add_library(mdds_example_types STATIC
        experiments/fastdds_socket/keyed/type/MddsKeyedSamplePubSubTypes.cxx
        experiments/fastdds_socket/keyed/type/MddsKeyedSampleTypeObjectSupport.cxx)
    target_include_directories(mdds_example_types PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/experiments/fastdds_socket/keyed/type>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/mdds/examples>)
    target_link_libraries(mdds_example_types PUBLIC mdds_fastdds_binding)
    list(APPEND _core mdds_example_types)
    # Remove source/output paths from __FILE__ and COFF diagnostic records only.
    # This does not alter generated serialization or runtime ownership logic.
    foreach(_mapped IN LISTS _core)
        if(_mapped STREQUAL "mdds_fastdds_binding")
            continue()
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _mdds_native_source)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" _mdds_native_build)
            target_compile_options(${_mapped} PRIVATE /experimental:deterministic
                "/pathmap:${_mdds_native_source}=." "/pathmap:${_mdds_native_build}=.build")
        elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            target_compile_options(${_mapped} PRIVATE "/clang:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "/clang:-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.build")
        else()
            target_compile_options(${_mapped} PRIVATE "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
                "-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=.build")
        endif()
    endforeach()
    install(TARGETS ${_core} EXPORT MDDSTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # Concrete facade layout and injection/probe access are private variant details.
    install(DIRECTORY include/mdds/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mdds
        FILES_MATCHING PATTERN "*.hpp" PATTERN "DSoftBusTransport.hpp" EXCLUDE
        PATTERN "Host*.hpp" EXCLUDE PATTERN "RuntimeProbeAccess.hpp" EXCLUDE PATTERN "ProviderAdapter.hpp" EXCLUDE
        PATTERN "ExperimentalRuntime.hpp" EXCLUDE)
    install(DIRECTORY experiments/fastdds_socket/keyed/type/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mdds/examples
        FILES_MATCHING PATTERN "*.hpp")
    install(EXPORT MDDSTargets NAMESPACE MDDS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MDDS)
    set(MDDS_HAS_EXPERIMENTAL FALSE)
    if(MDDS_BUILD_EXPERIMENTAL_RUNTIME)
        set(MDDS_HAS_EXPERIMENTAL TRUE)
        install(TARGETS mdds_experimental_runtime mdds_experimental_socket EXPORT MDDSExperimentalTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(FILES include/mdds/dsoftbus/ExperimentalRuntime.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mdds/dsoftbus)
        install(EXPORT MDDSExperimentalTargets NAMESPACE MDDS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MDDS)
    endif()
    configure_package_config_file(cmake/MDDSConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/MDDSConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MDDS)
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/MDDSConfigVersion.cmake" VERSION 0.1.0 COMPATIBILITY ExactVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MDDSConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/MDDSConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MDDS)
    install(DIRECTORY examples/installed_consumer DESTINATION ${CMAKE_INSTALL_DATADIR}/MDDS/examples)
    if(MDDS_BUILD_EXPERIMENTAL_RUNTIME)
        install(DIRECTORY examples/experimental_socket DESTINATION ${CMAKE_INSTALL_DATADIR}/MDDS/examples)
    endif()

endif()
