cmake_minimum_required(VERSION 3.22)
# Exact former public cache options only. Do not reject unrelated parent-project
# MDDS_* variables or silently reinterpret a request using the old interface.
foreach(_transport_legacy_option MDDS_BUILD_EXPERIMENTAL_RUNTIME MDDS_BUILD_EXPERIMENTAL_TESTS
        MDDS_EXPERIMENT_NATIVE MDDS_EXPERIMENT_RX_DROP MDDS_EXPERIMENT_TOKEN_BRIDGE
        MDDS_FASTDDS_MODE MDDS_INSTALL_PACKAGE MDDS_PUBLIC_SOCKET_LIBRARY)
    if(DEFINED ${_transport_legacy_option})
        string(REGEX REPLACE "^MDDS_" "FASTDDS_DSOFTBUS_" _transport_option_replacement "${_transport_legacy_option}")
        message(FATAL_ERROR "OBSOLETE_TRANSPORT_OPTION: ${_transport_legacy_option}; use ${_transport_option_replacement} in a fresh build directory")
    endif()
endforeach()
unset(_transport_legacy_option)
unset(_transport_option_replacement)
project(FastDDSDSoftBusTransport LANGUAGES C CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()



# Public Socket binding is inert until explicitly selected; never production registration.
option(BUILD_DSOFTBUS_PUBLIC_SOCKET_SHIM "Compile/link the public Socket shim for target audit only" OFF)
option(BUILD_DSOFTBUS_PUBLIC_SOCKET_FAKE_TESTS "Host tests of the same shim against Fake SDK symbols" OFF)
option(BUILD_PUBLIC_SOCKET_RUNTIME_PROBE "Build isolated opt-in target runtime probe, not a default provider" OFF)
option(BUILD_PUBLIC_SOCKET_RUNTIME_HOST_TESTS "Test isolated runtime assembly against Fake public C functions" OFF)
if(BUILD_DSOFTBUS_PUBLIC_SOCKET_SHIM AND BUILD_DSOFTBUS_PUBLIC_SOCKET_FAKE_TESTS)
    message(FATAL_ERROR "PUBLIC_SOCKET_VARIANTS_EXCLUSIVE: Fake SDK and public target cannot mix")
endif()

option(BUILD_INTERFACE_PROBE "Build the current Fast DDS interface probe using the selected binding" ON)
option(BUILD_DSOFTBUS_DIRECTORY "Build the DSoftBus logical address and route model" ON)
option(BUILD_DSOFTBUS_NATIVE_OWNER "Build the SDK-free stable native owner model" ${BUILD_DSOFTBUS_DIRECTORY})
option(BUILD_DSOFTBUS_SEND_QUEUE "Build the owned single/batch send queue core" ${BUILD_DSOFTBUS_DIRECTORY})
option(BUILD_DSOFTBUS_INPUT_CHANNELS "Build the receiver association and lifetime core" ${BUILD_DSOFTBUS_DIRECTORY})
option(BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS "Build the application receiver-generation core" ${BUILD_DSOFTBUS_INPUT_CHANNELS})
option(BUILD_DSOFTBUS_RUNTIME_SEND_PATH "Build the runtime and Fast DDS sender bridge" ${BUILD_DSOFTBUS_SEND_QUEUE})
set(_fastdds_dsoftbus_native_send_default OFF)
if(BUILD_DSOFTBUS_NATIVE_OWNER AND BUILD_DSOFTBUS_RUNTIME_SEND_PATH)
    set(_fastdds_dsoftbus_native_send_default ON)
endif()
option(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION "Build the SDK-free native send execution model" ${_fastdds_dsoftbus_native_send_default})
unset(_fastdds_dsoftbus_native_send_default)
set(_fastdds_dsoftbus_process_default OFF)
if(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION AND BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS)
    set(_fastdds_dsoftbus_process_default ON)
endif()
option(BUILD_DSOFTBUS_PROCESS "Build the SDK-free application/native coordination model" ${_fastdds_dsoftbus_process_default})
unset(_fastdds_dsoftbus_process_default)
# The option follows the directory model line like every sibling core, even
# though this target itself needs only Threads. Its test target compiles against
# the pinned DSoftBus public header for the callback-shape check, exactly like
# the existing ingress contract test.
option(BUILD_DSOFTBUS_SERVICE_RECORD_TABLE "Build the bounded shared service-record table model" ${BUILD_DSOFTBUS_DIRECTORY})
option(BUILD_DSOFTBUS_SERVICE_RECORD_BINDING "Build the record/native-owner/attachments composition layer" ${BUILD_DSOFTBUS_DIRECTORY})
option(BUILD_DSOFTBUS_TRANSPORT_FACADE "Build the opt-in facade against an actual Fast DDS CONFIG library" OFF)
option(BUILD_FASTDDS_DSOFTBUS_EXPERIMENT "Build the explicitly unqualified real Fast DDS Socket experiment" OFF)
option(BUILD_SOFTBUS_CAPABILITY_PROBE "Build the runtime Socket API capability probe" ON)
option(BUILD_NATIVE_PERMISSION_PROBE "Build the opt-in OpenHarmony native permission probe" OFF)
set(FASTDDS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Fast-DDS" CACHE PATH "Pinned Fast DDS source checkout")
set(DSOFTBUS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/communication_dsoftbus" CACHE PATH "Pinned DSoftBus source checkout")
set(FASTDDS_DSOFTBUS_FASTDDS_MODE "SOURCE_PROBE" CACHE STRING "Fast DDS binding: SOURCE_PROBE or CONFIG")
set_property(CACHE FASTDDS_DSOFTBUS_FASTDDS_MODE PROPERTY STRINGS SOURCE_PROBE CONFIG)
if(NOT FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "SOURCE_PROBE" AND NOT FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "CONFIG")
    message(FATAL_ERROR "FASTDDS_DSOFTBUS_FASTDDS_MODE must be SOURCE_PROBE or CONFIG; no implicit fallback")
endif()

if(BUILD_DSOFTBUS_NATIVE_OWNER AND NOT BUILD_DSOFTBUS_DIRECTORY)
    message(FATAL_ERROR "BUILD_DSOFTBUS_NATIVE_OWNER requires BUILD_DSOFTBUS_DIRECTORY")
endif()
if(BUILD_DSOFTBUS_SEND_QUEUE AND NOT BUILD_DSOFTBUS_DIRECTORY)
    message(FATAL_ERROR "BUILD_DSOFTBUS_SEND_QUEUE requires BUILD_DSOFTBUS_DIRECTORY")
endif()
if(BUILD_DSOFTBUS_INPUT_CHANNELS AND NOT BUILD_DSOFTBUS_DIRECTORY)
    message(FATAL_ERROR "BUILD_DSOFTBUS_INPUT_CHANNELS requires BUILD_DSOFTBUS_DIRECTORY")
endif()
if(BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS AND NOT BUILD_DSOFTBUS_INPUT_CHANNELS)
    message(FATAL_ERROR "BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS requires BUILD_DSOFTBUS_INPUT_CHANNELS")
endif()
if(BUILD_DSOFTBUS_RUNTIME_SEND_PATH AND NOT BUILD_DSOFTBUS_SEND_QUEUE)
    message(FATAL_ERROR "BUILD_DSOFTBUS_RUNTIME_SEND_PATH requires BUILD_DSOFTBUS_SEND_QUEUE")
endif()
if(BUILD_DSOFTBUS_SERVICE_RECORD_BINDING AND NOT BUILD_DSOFTBUS_SERVICE_RECORD_TABLE)
    message(FATAL_ERROR "BUILD_DSOFTBUS_SERVICE_RECORD_BINDING requires BUILD_DSOFTBUS_SERVICE_RECORD_TABLE")
endif()
if(BUILD_DSOFTBUS_SERVICE_RECORD_BINDING AND NOT BUILD_DSOFTBUS_PROCESS)
    message(FATAL_ERROR "BUILD_DSOFTBUS_SERVICE_RECORD_BINDING requires BUILD_DSOFTBUS_PROCESS")
endif()
if(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION AND NOT BUILD_DSOFTBUS_NATIVE_OWNER)
    message(FATAL_ERROR "BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION requires BUILD_DSOFTBUS_NATIVE_OWNER")
endif()
if(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION AND NOT BUILD_DSOFTBUS_RUNTIME_SEND_PATH)
    message(FATAL_ERROR "BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION requires BUILD_DSOFTBUS_RUNTIME_SEND_PATH")
endif()
if(BUILD_DSOFTBUS_PROCESS AND NOT BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION)
    message(FATAL_ERROR "BUILD_DSOFTBUS_PROCESS requires BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION")
endif()
if(BUILD_DSOFTBUS_PROCESS AND NOT BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS)
    message(FATAL_ERROR "BUILD_DSOFTBUS_PROCESS requires BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS")
endif()
if(BUILD_DSOFTBUS_TRANSPORT_FACADE)
    if(NOT FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "CONFIG")
        message(FATAL_ERROR "BUILD_DSOFTBUS_TRANSPORT_FACADE requires FASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG")
    endif()
    if(NOT BUILD_DSOFTBUS_INPUT_CHANNELS)
        message(FATAL_ERROR "BUILD_DSOFTBUS_TRANSPORT_FACADE requires BUILD_DSOFTBUS_INPUT_CHANNELS")
    endif()
    if(NOT BUILD_DSOFTBUS_RUNTIME_SEND_PATH)
        message(FATAL_ERROR "BUILD_DSOFTBUS_TRANSPORT_FACADE requires BUILD_DSOFTBUS_RUNTIME_SEND_PATH")
    endif()
endif()

if(BUILD_TESTING OR BUILD_NATIVE_PERMISSION_PROBE)
    add_library(native_permission_flow STATIC probes/native_permission_flow.cpp)
    target_include_directories(native_permission_flow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
endif()
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_library(softbus_probe_profile STATIC probes/softbus_probe_profile.cpp)
    target_include_directories(softbus_probe_profile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    add_executable(softbus_probe_profile_tests tests/softbus_probe_profile_tests.cpp)
    target_link_libraries(softbus_probe_profile_tests PRIVATE softbus_probe_profile softbus_record_contract)
    add_test(NAME softbus.probe_profile COMMAND softbus_probe_profile_tests)
    set_tests_properties(softbus.probe_profile PROPERTIES TIMEOUT 10)

    add_library(softbus_cancel_contract STATIC probes/softbus_cancel_contract.cpp)
    target_include_directories(softbus_cancel_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    target_link_libraries(softbus_cancel_contract PUBLIC Threads::Threads)
    add_executable(softbus_cancel_contract_tests tests/softbus_cancel_contract_tests.cpp)
    target_link_libraries(softbus_cancel_contract_tests PRIVATE softbus_cancel_contract)
    add_test(NAME softbus.cancel_contract COMMAND softbus_cancel_contract_tests)
    set_tests_properties(softbus.cancel_contract PROPERTIES TIMEOUT 10)

    add_library(softbus_record_contract STATIC probes/softbus_record_contract.cpp)
    target_include_directories(softbus_record_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    target_link_libraries(softbus_record_contract PUBLIC Threads::Threads)
    add_executable(softbus_record_contract_tests tests/softbus_record_contract_tests.cpp)
    target_link_libraries(softbus_record_contract_tests PRIVATE softbus_record_contract)
    add_test(NAME softbus.record_contract COMMAND softbus_record_contract_tests)
    set_tests_properties(softbus.record_contract PROPERTIES TIMEOUT 10)

    add_library(softbus_local_pair STATIC probes/softbus_local_pair.cpp)
    target_include_directories(softbus_local_pair PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    target_link_libraries(softbus_local_pair PUBLIC softbus_record_contract)
    add_executable(softbus_local_pair_tests tests/softbus_local_pair_tests.cpp)
    target_link_libraries(softbus_local_pair_tests PRIVATE softbus_local_pair)
    add_test(NAME softbus.local_pair COMMAND softbus_local_pair_tests)
    set_tests_properties(softbus.local_pair PROPERTIES TIMEOUT 10)

    add_library(softbus_ingress_contract STATIC probes/softbus_ingress_contract.cpp)
    target_include_directories(softbus_ingress_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    target_link_libraries(softbus_ingress_contract PUBLIC Threads::Threads)
    add_executable(softbus_ingress_contract_tests tests/softbus_ingress_contract_tests.cpp)
    target_link_libraries(softbus_ingress_contract_tests PRIVATE softbus_ingress_contract)
    target_include_directories(softbus_ingress_contract_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    foreach(_fastdds_dsoftbus_ingress_case unexposed exhaustion reattach retired same-table prelookup old-lease no-app pre-ingress bounded reentrant)
        add_test(NAME softbus.ingress.${_fastdds_dsoftbus_ingress_case}
            COMMAND softbus_ingress_contract_tests --case ${_fastdds_dsoftbus_ingress_case})
        set_tests_properties(softbus.ingress.${_fastdds_dsoftbus_ingress_case} PROPERTIES TIMEOUT 10)
    endforeach()

    add_library(softbus_options_contract STATIC probes/softbus_options_contract.cpp)
    target_include_directories(softbus_options_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    add_executable(softbus_options_contract_tests tests/softbus_options_contract_tests.cpp)
    target_link_libraries(softbus_options_contract_tests PRIVATE softbus_options_contract)
    add_test(NAME softbus.options_contract COMMAND softbus_options_contract_tests)
    set_tests_properties(softbus.options_contract PROPERTIES TIMEOUT 10)

    add_library(token_first_entry STATIC probes/token_first_entry.cpp)
    target_include_directories(token_first_entry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    add_executable(token_first_entry_tests tests/token_first_entry_tests.cpp)
    target_link_libraries(token_first_entry_tests PRIVATE token_first_entry)
    add_test(NAME softbus.token_first_entry COMMAND token_first_entry_tests)
    set_tests_properties(softbus.token_first_entry PROPERTIES TIMEOUT 10)

    add_library(softbus_lifecycle_contract STATIC probes/softbus_lifecycle_contract.cpp)
    target_include_directories(softbus_lifecycle_contract PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    target_link_libraries(softbus_lifecycle_contract PUBLIC softbus_record_contract Threads::Threads)
    add_executable(softbus_lifecycle_contract_tests tests/softbus_lifecycle_contract_tests.cpp)
    target_link_libraries(softbus_lifecycle_contract_tests PRIVATE softbus_lifecycle_contract)
    add_test(NAME softbus.lifecycle_contract COMMAND softbus_lifecycle_contract_tests)
    set_tests_properties(softbus.lifecycle_contract PROPERTIES TIMEOUT 15)

    if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
        # This concrete entry fixture uses Windows test boundaries only. The
        # native SDK probe is built separately and never gets this definition.
        add_executable(softbus_lifecycle_host_fixture probes/softbus_lifecycle_probe.cpp)
        target_compile_definitions(softbus_lifecycle_host_fixture PRIVATE FASTDDS_DSOFTBUS_LIFECYCLE_HOST_FIXTURE=1)
        target_link_libraries(softbus_lifecycle_host_fixture PRIVATE softbus_lifecycle_contract token_first_entry)
    endif()

    if(NOT CMAKE_CROSSCOMPILING)
        find_program(RECORD_HARNESS_NODE node)
        if(RECORD_HARNESS_NODE)
            add_test(NAME softbus.native_wait_observer COMMAND "${RECORD_HARNESS_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_wait_observer_tests.cjs")
            set_tests_properties(softbus.native_wait_observer PROPERTIES TIMEOUT 10)
            add_test(NAME softbus.record_pair_harness COMMAND "${RECORD_HARNESS_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/record_pair_harness_tests.cjs")
            set_tests_properties(softbus.record_pair_harness PROPERTIES TIMEOUT 10)
            add_test(NAME softbus.record_run_binding COMMAND "${RECORD_HARNESS_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/record_run_binding_tests.cjs")
            set_tests_properties(softbus.record_run_binding PROPERTIES TIMEOUT 10)
            add_test(NAME softbus.record_v1_compatibility COMMAND "${RECORD_HARNESS_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/record_v1_compatibility_tests.cjs")
            set_tests_properties(softbus.record_v1_compatibility PROPERTIES TIMEOUT 10)
            add_test(NAME softbus.lifecycle_run_binding COMMAND "${RECORD_HARNESS_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/lifecycle_run_binding_tests.cjs")
            set_tests_properties(softbus.lifecycle_run_binding PROPERTIES TIMEOUT 10)
            if(TARGET softbus_lifecycle_host_fixture)
                add_test(NAME softbus.lifecycle_subprocess COMMAND "${RECORD_HARNESS_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/softbus_lifecycle_subprocess_tests.cjs"
                    "$<TARGET_FILE:softbus_lifecycle_host_fixture>")
                # Aggregate of 11 cases, each with its existing 10-second
                # watchdog; no single SDK/probe budget is enlarged here.
                set_tests_properties(softbus.lifecycle_subprocess PROPERTIES TIMEOUT 120)
            endif()
        else()
            message(WARNING "Node unavailable: record harness/run-binding/compatibility and lifecycle binding/subprocess host tests are NOT RUN")
        endif()
    endif()

    add_executable(native_permission_tests tests/native_permission_tests.cpp)
    target_link_libraries(native_permission_tests PRIVATE native_permission_flow)
    add_test(NAME permissions.flow COMMAND native_permission_tests)
    set_tests_properties(permissions.flow PROPERTIES TIMEOUT 10)
    if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
        find_program(PERMISSION_POWERSHELL powershell REQUIRED)
        add_test(NAME experiment_build.source_state COMMAND "${PERMISSION_POWERSHELL}"
            -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/experimental_source_state_tests.ps1")
        set_tests_properties(experiment_build.source_state PROPERTIES TIMEOUT 10)
        add_test(NAME experiment_build.dependencies COMMAND "${PERMISSION_POWERSHELL}"
            -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/experimental_build_inputs_tests.ps1")
        set_tests_properties(experiment_build.dependencies PROPERTIES TIMEOUT 10)
        add_test(NAME permissions.snapshot_guard COMMAND "${PERMISSION_POWERSHELL}"
            -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_permission_guard_tests.ps1"
            -RunLabel ctest)
        set_tests_properties(permissions.snapshot_guard PROPERTIES TIMEOUT 30)
    endif()
endif()

if(BUILD_NATIVE_PERMISSION_PROBE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        message(FATAL_ERROR "Native permission probe requires the actual OpenHarmony toolchain")
    endif()
    set(ACCESS_TOKEN_SOURCE_DIR "" CACHE PATH "Pinned official security_access_token reference checkout")
    set(NATIVE_TOKEN_LIBRARY "" CACHE FILEPATH "Verified board libnativetoken_shared.z.so snapshot")
    set(TOKEN_SETPROC_LIBRARY "" CACHE FILEPATH "Verified board libtokensetproc_shared.z.so snapshot")
    set(ACCESS_TOKEN_LIBRARY "" CACHE FILEPATH "Verified board libaccesstoken_sdk.z.so snapshot")
    foreach(required IN ITEMS NATIVE_TOKEN_LIBRARY TOKEN_SETPROC_LIBRARY ACCESS_TOKEN_LIBRARY)
        if(NOT IS_ABSOLUTE "${${required}}" OR NOT EXISTS "${${required}}")
            message(FATAL_ERROR "${required} must name an explicit existing board library snapshot")
        endif()
    endforeach()
    if(NOT EXISTS "${ACCESS_TOKEN_SOURCE_DIR}/interfaces/innerkits/nativetoken/include/nativetoken_kit.h")
        message(FATAL_ERROR "ACCESS_TOKEN_SOURCE_DIR must provide the actual official headers")
    endif()
    add_executable(native_permission_probe probes/native_permission_probe.cpp)
    target_include_directories(native_permission_probe PRIVATE
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
        "${ACCESS_TOKEN_SOURCE_DIR}/interfaces/innerkits/nativetoken/include"
        "${ACCESS_TOKEN_SOURCE_DIR}/interfaces/innerkits/token_setproc/include"
        "${ACCESS_TOKEN_SOURCE_DIR}/interfaces/innerkits/accesstoken/include")
    target_link_libraries(native_permission_probe PRIVATE native_permission_flow
        "${NATIVE_TOKEN_LIBRARY}" "${TOKEN_SETPROC_LIBRARY}" "${ACCESS_TOKEN_LIBRARY}" ${CMAKE_DL_LIBS})
    # Board providers resolve their own transitive system dependencies on-device.
    # The probe's direct symbols must still resolve against the supplied snapshots.
    target_link_options(native_permission_probe PRIVATE -Wl,--allow-shlib-undefined)
endif()

if(BUILD_INTERFACE_PROBE OR BUILD_DSOFTBUS_DIRECTORY)
    include(cmake/FastddsBinding.cmake)
endif()

if(BUILD_DSOFTBUS_DIRECTORY)
    add_library(fastdds_dsoftbus_directory STATIC src/EndpointDirectory.cpp src/V1Address.cpp src/V1Configuration.cpp)
    target_include_directories(fastdds_dsoftbus_directory PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")
    target_link_libraries(fastdds_dsoftbus_directory PUBLIC fastdds_dsoftbus_fastdds_binding)
    target_compile_features(fastdds_dsoftbus_directory PUBLIC cxx_std_17)
    if(FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "SOURCE_PROBE")
        # Preserve standalone inline-value behavior; never override a real DLL import.
        target_compile_definitions(fastdds_dsoftbus_directory PRIVATE FASTDDS_STATIC_LINK)
    endif()
    if(WIN32)
        target_compile_definitions(fastdds_dsoftbus_directory PUBLIC NOMINMAX)
    endif()
    if(BUILD_TESTING)
        find_package(Threads REQUIRED)
        add_executable(endpoint_directory_tests tests/endpoint_directory_tests.cpp)
        target_link_libraries(endpoint_directory_tests PRIVATE fastdds_dsoftbus_directory Threads::Threads)
        add_test(NAME dsoftbus.endpoint_directory COMMAND endpoint_directory_tests)
        add_executable(v1_address_tests tests/v1_address_tests.cpp)
        target_link_libraries(v1_address_tests PRIVATE fastdds_dsoftbus_directory)
        add_test(NAME dsoftbus.v1_address COMMAND v1_address_tests)
        set_tests_properties(dsoftbus.v1_address PROPERTIES TIMEOUT 15)
        add_executable(v1_configuration_tests tests/v1_configuration_tests.cpp)
        target_link_libraries(v1_configuration_tests PRIVATE fastdds_dsoftbus_directory)
        add_test(NAME dsoftbus.v1_configuration COMMAND v1_configuration_tests)
        set_tests_properties(dsoftbus.v1_configuration PROPERTIES TIMEOUT 15)
        set_tests_properties(dsoftbus.endpoint_directory PROPERTIES TIMEOUT 15)
    endif()
endif()

if(BUILD_DSOFTBUS_NATIVE_OWNER)
    find_package(Threads REQUIRED)
    add_library(fastdds_dsoftbus_native_owner STATIC src/NativeOwnerTable.cpp)
    target_link_libraries(fastdds_dsoftbus_native_owner PUBLIC fastdds_dsoftbus_directory Threads::Threads)
    target_compile_features(fastdds_dsoftbus_native_owner PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(native_owner_table_tests tests/native_owner_table_tests.cpp)
        target_link_libraries(native_owner_table_tests PRIVATE fastdds_dsoftbus_native_owner)
        add_test(NAME dsoftbus.native_owner_model COMMAND native_owner_table_tests)
        set_tests_properties(dsoftbus.native_owner_model PROPERTIES TIMEOUT 20)
    endif()
endif()

if(BUILD_DSOFTBUS_SERVICE_RECORD_BINDING)
    find_package(Threads REQUIRED)
    add_library(fastdds_dsoftbus_service_record_binding STATIC src/ServiceRecordBinding.cpp)
    target_link_libraries(fastdds_dsoftbus_service_record_binding PUBLIC
        fastdds_dsoftbus_service_record_table fastdds_dsoftbus_process Threads::Threads)
    target_compile_features(fastdds_dsoftbus_service_record_binding PUBLIC cxx_std_17)
    # The composition's delivery handle must expose no execution entry of its own,
    # so a delivery cannot be entered around the context the composition arms on
    # the admitted execution. Both controls compile one translation unit with the
    # same include path, compiler and flags, compile-only so nothing links: the
    # legal call site must build, the bypassing one must not, and it must fail on
    # the missing member rather than on a missing header or a link error.
    # The composition header reaches Fast DDS through the directory model, so both
    # controls compile against the include closure the composition itself uses.
    set(_fastdds_dsoftbus_binding_check_includes "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(TARGET fastdds)
        get_target_property(_fastdds_dsoftbus_binding_imported_includes fastdds INTERFACE_INCLUDE_DIRECTORIES)
        if(_fastdds_dsoftbus_binding_imported_includes)
            list(APPEND _fastdds_dsoftbus_binding_check_includes ${_fastdds_dsoftbus_binding_imported_includes})
        endif()
    endif()
    if(TARGET fastdds_dsoftbus_fastdds_binding)
        get_target_property(_fastdds_dsoftbus_binding_probe_includes fastdds_dsoftbus_fastdds_binding INTERFACE_INCLUDE_DIRECTORIES)
        if(_fastdds_dsoftbus_binding_probe_includes)
            list(APPEND _fastdds_dsoftbus_binding_check_includes ${_fastdds_dsoftbus_binding_probe_includes})
        endif()
    endif()
    # Compile-only: neither control may be required to link, so a result can only
    # come from the type and never from a missing symbol.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    try_compile(_fastdds_dsoftbus_binding_legal_compiled
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/delivery_context_legal.cpp"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_fastdds_dsoftbus_binding_check_includes}"
        OUTPUT_VARIABLE _fastdds_dsoftbus_binding_legal_output)
    if(NOT _fastdds_dsoftbus_binding_legal_compiled)
        message(FATAL_ERROR
            "the composition's legal delivery call site must compile:\n${_fastdds_dsoftbus_binding_legal_output}")
    endif()
    try_compile(_fastdds_dsoftbus_binding_bypass_compiled
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/delivery_context_bypass.cpp"
        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_fastdds_dsoftbus_binding_check_includes}"
        OUTPUT_VARIABLE _fastdds_dsoftbus_binding_bypass_output)
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
    if(_fastdds_dsoftbus_binding_bypass_compiled)
        message(FATAL_ERROR
            "the composition's delivery handle exposed an execution entry: a direct Run() compiled")
    endif()
    if(NOT _fastdds_dsoftbus_binding_bypass_output MATCHES "C2039|has no member|no member named")
        message(FATAL_ERROR
            "the bypassing call site failed for an unrelated reason:\n${_fastdds_dsoftbus_binding_bypass_output}")
    endif()
    if(BUILD_TESTING)
        # delivery_context_legal.cpp is compiled into the target, so the
        # delivery handle's encapsulation is a build-time fact with a real header
        # dependency and cannot be skipped by an incremental build.
        add_executable(service_record_binding_tests tests/service_record_binding_tests.cpp
            tests/delivery_context_legal.cpp)
        target_link_libraries(service_record_binding_tests PRIVATE fastdds_dsoftbus_service_record_binding)
        target_include_directories(service_record_binding_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
        foreach(_fastdds_dsoftbus_service_record_binding_case decision-accounting decision-store-bound decision-budget decision-close-request decision-paused-drain two-owners cross-role partial-failure
                close-vs-capture admitted-not-run running-receiver unique-close close-credit-same close-credit-different shutdown-reentrant-sibling shutdown-destroy-same-table shutdown-destroy-other-table reentrant-boundary
                exceptional-admission old-entry-new-incarnation repeated-lifecycles slot-exhaustion
                resource-release fail-closed creator-exits-joiner-lives delivery-context-bypass
                shared-group-release-race join-during-settle joiner-failure-keeps-creator
                registration-allocation-failure accepted-topology prepared-accepted
                second-binding-same-plan rejected-accepted-closed joiner-refuses-unanchored
                close-request-from-callback close-sync-refused-in-callback
                close-completion-guard-in-callback close-completes-outside-callback close-after-callback-exception
                many-close-requests-one-close no-admission-after-close-request
                close-request-releases-resources callback-destroy-fails-fast
                destroy-unrelated-in-callback destroy-shared-sibling-in-callback)
            add_test(NAME dsoftbus.service_record_binding.${_fastdds_dsoftbus_service_record_binding_case}
                COMMAND service_record_binding_tests --case ${_fastdds_dsoftbus_service_record_binding_case})
            set_tests_properties(dsoftbus.service_record_binding.${_fastdds_dsoftbus_service_record_binding_case}
                PROPERTIES TIMEOUT 60)
        endforeach()
    endif()
endif()

if(BUILD_DSOFTBUS_SERVICE_RECORD_TABLE)
    find_package(Threads REQUIRED)
    add_library(fastdds_dsoftbus_service_record_table STATIC src/ServiceRecordTable.cpp)
    target_include_directories(fastdds_dsoftbus_service_record_table PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:include>")
    target_link_libraries(fastdds_dsoftbus_service_record_table PUBLIC Threads::Threads)
    target_compile_features(fastdds_dsoftbus_service_record_table PUBLIC cxx_std_17)
    # The bounded-counter test hooks exist only in this library and its test
    # target; a production consumer's header has no test declarations at all.
    target_compile_definitions(fastdds_dsoftbus_service_record_table PRIVATE FASTDDS_DSOFTBUS_SERVICE_RECORD_TABLE_TEST_HOOKS)
    if(BUILD_TESTING)
        add_executable(service_record_table_tests tests/service_record_table_tests.cpp)
        target_link_libraries(service_record_table_tests PRIVATE fastdds_dsoftbus_service_record_table)
        target_compile_definitions(service_record_table_tests PRIVATE FASTDDS_DSOFTBUS_SERVICE_RECORD_TABLE_TEST_HOOKS)
        target_include_directories(service_record_table_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
        foreach(_fastdds_dsoftbus_service_record_case unexposed shared-role sibling-release cross-role unanchored
                destroy-replace still-anchored exhaustion failed-registration capacity handles reentrant
                concurrent identity-boundary allocation-failure)
            add_test(NAME dsoftbus.service_record.${_fastdds_dsoftbus_service_record_case}
                COMMAND service_record_table_tests --case ${_fastdds_dsoftbus_service_record_case})
            set_tests_properties(dsoftbus.service_record.${_fastdds_dsoftbus_service_record_case} PROPERTIES TIMEOUT 30)
        endforeach()
    endif()
endif()

if(BUILD_DSOFTBUS_SEND_QUEUE)
    find_package(Threads REQUIRED)
    add_library(fastdds_dsoftbus_send_queue STATIC src/SendQueue.cpp)
    target_link_libraries(fastdds_dsoftbus_send_queue PUBLIC fastdds_dsoftbus_directory Threads::Threads)
    target_compile_features(fastdds_dsoftbus_send_queue PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(send_queue_tests tests/send_queue_tests.cpp)
        target_link_libraries(send_queue_tests PRIVATE fastdds_dsoftbus_send_queue)
        add_test(NAME dsoftbus.send_queue COMMAND send_queue_tests)
        set_tests_properties(dsoftbus.send_queue PROPERTIES TIMEOUT 15)
    endif()
endif()

if(BUILD_DSOFTBUS_INPUT_CHANNELS)
    find_package(Threads REQUIRED)
    add_library(fastdds_dsoftbus_input_channels STATIC src/InputChannels.cpp)
    target_link_libraries(fastdds_dsoftbus_input_channels PUBLIC fastdds_dsoftbus_directory Threads::Threads)
    target_compile_features(fastdds_dsoftbus_input_channels PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(input_channels_tests tests/input_channels_tests.cpp)
        target_link_libraries(input_channels_tests PRIVATE fastdds_dsoftbus_input_channels)
        add_test(NAME dsoftbus.input_channels COMMAND input_channels_tests)
        set_tests_properties(dsoftbus.input_channels PROPERTIES TIMEOUT 15)
    endif()
endif()

if(BUILD_DSOFTBUS_APPLICATION_ATTACHMENTS)
    add_library(fastdds_dsoftbus_application_attachments STATIC src/ApplicationAttachments.cpp)
    target_link_libraries(fastdds_dsoftbus_application_attachments PUBLIC fastdds_dsoftbus_input_channels)
    target_compile_features(fastdds_dsoftbus_application_attachments PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(application_attachments_tests tests/application_attachments_tests.cpp)
        target_link_libraries(application_attachments_tests PRIVATE fastdds_dsoftbus_application_attachments)
        add_test(NAME dsoftbus.application_attachments COMMAND application_attachments_tests)
        set_tests_properties(dsoftbus.application_attachments PROPERTIES TIMEOUT 20)
    endif()
endif()

if(BUILD_DSOFTBUS_RUNTIME_SEND_PATH)
    add_library(fastdds_dsoftbus_runtime_send_path STATIC
        src/RuntimeSendPath.cpp src/DSoftBusSenderResource.cpp)
    target_link_libraries(fastdds_dsoftbus_runtime_send_path PUBLIC fastdds_dsoftbus_send_queue Threads::Threads)
    target_compile_features(fastdds_dsoftbus_runtime_send_path PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(runtime_send_path_tests tests/runtime_send_path_tests.cpp)
        target_link_libraries(runtime_send_path_tests PRIVATE fastdds_dsoftbus_runtime_send_path)
        add_test(NAME dsoftbus.runtime_send_path COMMAND runtime_send_path_tests)
        set_tests_properties(dsoftbus.runtime_send_path PROPERTIES TIMEOUT 15)
    endif()
endif()

if(BUILD_DSOFTBUS_NATIVE_SEND_EXECUTION)
    add_library(fastdds_dsoftbus_native_send_execution STATIC src/NativeSendExecution.cpp)
    target_link_libraries(fastdds_dsoftbus_native_send_execution PUBLIC
        fastdds_dsoftbus_native_owner fastdds_dsoftbus_runtime_send_path)
    target_compile_features(fastdds_dsoftbus_native_send_execution PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(native_send_execution_tests tests/native_send_execution_tests.cpp)
        target_link_libraries(native_send_execution_tests PRIVATE fastdds_dsoftbus_native_send_execution)
        add_test(NAME dsoftbus.native_send_execution COMMAND native_send_execution_tests)
        set_tests_properties(dsoftbus.native_send_execution PROPERTIES TIMEOUT 30)
    endif()
endif()

if(BUILD_DSOFTBUS_PROCESS)
    add_library(fastdds_dsoftbus_process STATIC src/ProcessReceivePath.cpp src/ProcessCoordinator.cpp)
    target_link_libraries(fastdds_dsoftbus_process PUBLIC
        fastdds_dsoftbus_native_send_execution fastdds_dsoftbus_application_attachments)
    target_compile_features(fastdds_dsoftbus_process PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        add_executable(process_receive_tests tests/process_receive_tests.cpp)
        target_link_libraries(process_receive_tests PRIVATE fastdds_dsoftbus_process)
        add_test(NAME dsoftbus.process_receive COMMAND process_receive_tests)
        set_tests_properties(dsoftbus.process_receive PROPERTIES TIMEOUT 30)
        add_executable(process_coordinator_tests tests/process_coordinator_tests.cpp)
        target_link_libraries(process_coordinator_tests PRIVATE fastdds_dsoftbus_process)
        add_test(NAME dsoftbus.process_coordinator COMMAND process_coordinator_tests)
        set_tests_properties(dsoftbus.process_coordinator PROPERTIES TIMEOUT 30)
    endif()
endif()

if(BUILD_DSOFTBUS_TRANSPORT_FACADE)
    add_library(fastdds_dsoftbus_transport_facade STATIC
        src/DSoftBusTransportDescriptor.cpp src/DSoftBusTransport.cpp)
    target_link_libraries(fastdds_dsoftbus_transport_facade PUBLIC
        fastdds_dsoftbus_runtime_send_path fastdds_dsoftbus_input_channels)
    target_compile_features(fastdds_dsoftbus_transport_facade PUBLIC cxx_std_17)
    set_property(TARGET fastdds_dsoftbus_transport_facade PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT production)
    set_property(TARGET fastdds_dsoftbus_transport_facade APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
    if(BUILD_TESTING)
        add_executable(transport_facade_tests tests/transport_facade_tests.cpp)
        target_link_libraries(transport_facade_tests PRIVATE fastdds_dsoftbus_transport_facade)
        add_test(NAME dsoftbus.transport_facade COMMAND transport_facade_tests)
        set_tests_properties(dsoftbus.transport_facade PROPERTIES TIMEOUT 15)
    endif()
endif()

# An isolated manually driven Host consumer; never link both facade variants.
option(BUILD_DSOFTBUS_HOST_PROVIDER_LOOP "Build the explicit Host facade/provider loop" OFF)
if(BUILD_DSOFTBUS_HOST_PROVIDER_LOOP)
    if(NOT FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "CONFIG" OR NOT BUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0)
        message(FATAL_ERROR "Host provider loop requires CONFIG and ProviderAdapter Phase0")
    endif()
    add_library(fastdds_dsoftbus_host_injected_facade STATIC src/DSoftBusTransportDescriptor.cpp src/DSoftBusTransport.cpp)
    target_link_libraries(fastdds_dsoftbus_host_injected_facade PUBLIC fastdds_dsoftbus_runtime_send_path fastdds_dsoftbus_input_channels)
    target_compile_definitions(fastdds_dsoftbus_host_injected_facade PUBLIC FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING=1)
    set_property(TARGET fastdds_dsoftbus_host_injected_facade PROPERTY INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT host_injected)
    set_property(TARGET fastdds_dsoftbus_host_injected_facade APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)
    add_library(fastdds_dsoftbus_host_provider_loop STATIC src/HostProviderLoop.cpp)
    target_link_libraries(fastdds_dsoftbus_host_provider_loop PUBLIC fastdds_dsoftbus_host_injected_facade fastdds_dsoftbus_provider_adapter_phase0)
    if(BUILD_TESTING)
        add_executable(host_provider_loop_tests tests/host_provider_loop_tests.cpp)
        target_link_libraries(host_provider_loop_tests PRIVATE fastdds_dsoftbus_host_provider_loop)
        add_executable(v1_production_tests tests/v1_production_tests.cpp)
        target_link_libraries(v1_production_tests PRIVATE fastdds_dsoftbus_host_provider_loop)
        add_test(NAME dsoftbus.v1_production COMMAND v1_production_tests)
        set_tests_properties(dsoftbus.v1_production PROPERTIES TIMEOUT 30)
        add_executable(runtime_shutdown_observer_tests tests/runtime_shutdown_observer_tests.cpp)
        target_link_libraries(runtime_shutdown_observer_tests PRIVATE fastdds_dsoftbus_host_provider_loop)
        foreach(_shutdown_case once throw nested shared active)
            add_test(NAME dsoftbus.runtime_shutdown_observer.${_shutdown_case} COMMAND runtime_shutdown_observer_tests ${_shutdown_case})
            set_tests_properties(dsoftbus.runtime_shutdown_observer.${_shutdown_case} PROPERTIES TIMEOUT 30)
        endforeach()
        add_executable(host_shared_channel_tests tests/host_shared_channel_tests.cpp)
        target_link_libraries(host_shared_channel_tests PRIVATE fastdds_dsoftbus_host_provider_loop)
        foreach(_shared_case send-classification send-control-ledger cancel-taken cancel-admitted shared creator controller-released reattach queued running foreign foreign-peer-records foreign-peer-bytes foreign-global-records foreign-global-bytes foreign-record-bound-legal old-snapshot budget open-close-race concurrent-first concurrent-close copying own-lease candidate-order capture-accounting pinned-close admitted rollback sending join-last egress-settled egress-queued egress-old-resource pump-authority budget-two-views budget-ceiling)
            add_test(NAME dsoftbus.host_shared_channel.${_shared_case} COMMAND host_shared_channel_tests ${_shared_case})
            set_tests_properties(dsoftbus.host_shared_channel.${_shared_case} PROPERTIES TIMEOUT 20)
        endforeach()
        add_executable(host_participant_tests tests/host_participant_tests.cpp)
        # Reuse the existing bounded generated type, without the old Transport/fixture.
        target_sources(host_participant_tests PRIVATE
            experiments/fastdds_socket/keyed/type/DSoftBusKeyedSamplePubSubTypes.cxx
            experiments/fastdds_socket/keyed/type/DSoftBusKeyedSampleTypeObjectSupport.cxx)
        target_include_directories(host_participant_tests PRIVATE experiments/fastdds_socket/keyed/type)
        target_link_libraries(host_participant_tests PRIVATE fastdds_dsoftbus_host_provider_loop fastcdr)
        foreach(_dds_case factory default normal-b-first normal-a-first disabled-link failure oracle pump-fault late-extra)
            add_test(NAME dsoftbus.host_participant.${_dds_case} COMMAND host_participant_tests ${_dds_case})
            set_tests_properties(dsoftbus.host_participant.${_dds_case} PROPERTIES TIMEOUT 45)
        endforeach()
        add_executable(host_input_close_tests tests/host_input_close_tests.cpp)
        target_link_libraries(host_input_close_tests PRIVATE fastdds_dsoftbus_host_provider_loop)
        foreach(_close_case basic destroy queued copy running sending gap same-address reopen-failure locators fault allocation-sweep callback-control callback-during-close)
            add_test(NAME dsoftbus.host_input_close.${_close_case} COMMAND host_input_close_tests ${_close_case})
            set_tests_properties(dsoftbus.host_input_close.${_close_case} PROPERTIES TIMEOUT 20)
        endforeach()
        foreach(_loop_case default loop preopen input-close queued paused-send paused-receiver late error
                open-exception open-allocation open-racing-shutdown backend-size-bound backend-size-above backend-size-unspecified observe-lifetime shared-init startup-allocation setup-cleanup-failure receive-limit)
            add_test(NAME dsoftbus.host_provider_loop.${_loop_case} COMMAND host_provider_loop_tests ${_loop_case})
            set_tests_properties(dsoftbus.host_provider_loop.${_loop_case} PROPERTIES TIMEOUT 20)
        endforeach()
        add_test(NAME dsoftbus.host_provider_loop.variants COMMAND "${CMAKE_COMMAND}"
            "-DROOT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/variants"
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DFASTDDS_DIR=${fastdds_DIR}"
            "-DFASTCDR_DIR=${fastcdr_DIR}" "-DFOONATHAN_DIR=${foonathan_memory_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_host_facade_variants.cmake")
        set_tests_properties(dsoftbus.host_provider_loop.variants PROPERTIES TIMEOUT 180)
    endif()
endif()

if(BUILD_INTERFACE_PROBE)
    find_package(Threads REQUIRED)
    # Always an interface probe, not the production transport. In CONFIG the
    # actual upstream implementation comes solely from the imported library.
    add_library(fastdds_interface_probe STATIC probes/fastdds_interface_probe.cpp)
    target_include_directories(fastdds_interface_probe PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/probes")
    if(FASTDDS_DSOFTBUS_FASTDDS_MODE STREQUAL "SOURCE_PROBE")
        target_sources(fastdds_interface_probe PRIVATE
            "${FASTDDS_SOURCE_DIR}/src/cpp/rtps/transport/TransportInterface.cpp")
        target_compile_definitions(fastdds_interface_probe PUBLIC FASTDDS_STATIC_LINK)
    endif()
    if(WIN32)
        target_compile_definitions(fastdds_interface_probe PUBLIC NOMINMAX)
    endif()
    target_link_libraries(fastdds_interface_probe PUBLIC fastdds_dsoftbus_fastdds_binding Threads::Threads)
    add_executable(fastdds_interface_tests tests/fastdds_interface_tests.cpp)
    target_link_libraries(fastdds_interface_tests PRIVATE fastdds_interface_probe)
    if(BUILD_TESTING)
        add_test(NAME fastdds.interface_contract COMMAND fastdds_interface_tests)
        set_tests_properties(fastdds.interface_contract PROPERTIES TIMEOUT 15)
    endif()
endif()

if(BUILD_SOFTBUS_CAPABILITY_PROBE)
    set(SOFTBUS_PUBLIC_INCLUDE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    add_executable(softbus_capability_probe probes/softbus_capability_probe.cpp)
    target_include_directories(softbus_capability_probe PRIVATE "${SOFTBUS_PUBLIC_INCLUDE}")
    target_link_libraries(softbus_capability_probe PRIVATE ${CMAKE_DL_LIBS})
    if(BUILD_TESTING)
        # Test-only providers are never deployed to a board as DSoftBus libraries.
        add_library(softbus_fixture SHARED tests/softbus_fixture.cpp)
        add_library(softbus_incomplete_fixture SHARED tests/softbus_fixture.cpp)
        add_library(softbus_success_fixture SHARED tests/softbus_fixture.cpp)
        target_compile_definitions(softbus_success_fixture PRIVATE SOCKET_SUCCEEDS)
        target_compile_definitions(softbus_incomplete_fixture PRIVATE OMIT_SEND_BYTES)
        foreach(fixture IN ITEMS softbus_fixture softbus_incomplete_fixture softbus_success_fixture)
            target_include_directories(${fixture} PRIVATE "${SOFTBUS_PUBLIC_INCLUDE}")
            set_target_properties(${fixture} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        endforeach()
        foreach(case IN ITEMS missing_library missing_required optional_absent socket_rejected socket_closed usage)
            add_test(NAME softbus.capability.${case}
                COMMAND "${CMAKE_COMMAND}"
                    "-DPROBE=$<TARGET_FILE:softbus_capability_probe>"
                    "-DFIXTURE=$<TARGET_FILE:softbus_fixture>"
                    "-DINCOMPLETE=$<TARGET_FILE:softbus_incomplete_fixture>"
                    "-DSUCCESS=$<TARGET_FILE:softbus_success_fixture>"
                    "-DCASE=${case}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_capability.cmake")
            set_tests_properties(softbus.capability.${case} PROPERTIES TIMEOUT 10)
        endforeach()
    endif()
endif()

# This isolated gate never changes the production descriptor/factory.
if(BUILD_FASTDDS_DSOFTBUS_EXPERIMENT)
    add_subdirectory(experiments/fastdds_socket)
endif()

# Phase-0 boundary prototype: explicit opt-in, no production registration.
option(BUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0 "Build the SDK-free Host provider boundary prototype" OFF)
if(BUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0)
    if(NOT BUILD_DSOFTBUS_SERVICE_RECORD_BINDING)
        message(FATAL_ERROR "Provider phase0 requires the existing service-record binding")
    endif()
    add_library(fastdds_dsoftbus_provider_adapter_phase0 STATIC src/ProviderAdapter.cpp)
    set_property(TARGET fastdds_dsoftbus_provider_adapter_phase0 PROPERTY INTERFACE_FASTDDS_DSOFTBUS_PROVIDER_ADAPTER_VARIANT default)
    set_property(TARGET fastdds_dsoftbus_provider_adapter_phase0 APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_PROVIDER_ADAPTER_VARIANT)
    target_link_libraries(fastdds_dsoftbus_provider_adapter_phase0 PUBLIC fastdds_dsoftbus_service_record_binding)
    target_compile_features(fastdds_dsoftbus_provider_adapter_phase0 PUBLIC cxx_std_17)
    if(BUILD_TESTING)
        if(NOT BUILD_DSOFTBUS_TRANSPORT_FACADE)
            message(FATAL_ERROR "Phase0 P0 test requires the real default facade (CONFIG)")
        endif()
        add_executable(provider_adapter_tests tests/provider_adapter_tests.cpp)
        target_link_libraries(provider_adapter_tests PRIVATE fastdds_dsoftbus_provider_adapter_phase0 fastdds_dsoftbus_transport_facade)
        foreach(_provider_case decision-branches P0 P1 P2 P3 P4 P5 P6 P7 early-stop provider-throw listen
                create-stop create-foreign create-conflict shutdown-failure ingress-allocation setup-observe)
            add_test(NAME dsoftbus.provider_phase0.${_provider_case} COMMAND provider_adapter_tests ${_provider_case})
            set_tests_properties(dsoftbus.provider_phase0.${_provider_case} PROPERTIES TIMEOUT 15)
        endforeach()
    endif()
endif()

if(BUILD_DSOFTBUS_PUBLIC_SOCKET_FAKE_TESTS)
    if(NOT BUILD_TESTING OR CMAKE_CROSSCOMPILING OR NOT BUILD_DSOFTBUS_HOST_PROVIDER_LOOP)
        message(FATAL_ERROR "Public Socket Fake SDK tests require the explicit Host loop")
    endif()
    add_library(fastdds_dsoftbus_public_socket_shim_fake STATIC src/PublicSocketBackend.cpp)
    target_compile_definitions(fastdds_dsoftbus_public_socket_shim_fake PUBLIC FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK=1)
    if(MSVC)
        # The Fake SDK deliberately throws from extern-C test symbols. /EHsc
        # assumes such calls cannot throw, unlike this explicit Host fault model.
        target_compile_options(fastdds_dsoftbus_public_socket_shim_fake PRIVATE /EHsc-)
    endif()
    target_include_directories(fastdds_dsoftbus_public_socket_shim_fake PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(fastdds_dsoftbus_public_socket_shim_fake PUBLIC fastdds_dsoftbus_provider_adapter_phase0)
    set_property(TARGET fastdds_dsoftbus_public_socket_shim_fake PROPERTY INTERFACE_FASTDDS_DSOFTBUS_PUBLIC_SOCKET_VARIANT fake)
    set_property(TARGET fastdds_dsoftbus_public_socket_shim_fake APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_PUBLIC_SOCKET_VARIANT)
    add_executable(public_socket_shim_tests tests/public_socket_shim_tests.cpp)
    add_executable(public_socket_accepted_tests tests/public_socket_accepted_tests.cpp)
    target_include_directories(public_socket_accepted_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(public_socket_accepted_tests PRIVATE fastdds_dsoftbus_public_socket_shim_fake fastdds_dsoftbus_host_provider_loop)
    add_executable(public_socket_dds_tests tests/public_socket_dds_tests.cpp tests/FakePublicSocketLink.cpp
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSamplePubSubTypes.cxx
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSampleTypeObjectSupport.cxx)
    target_include_directories(public_socket_dds_tests PRIVATE experiments/fastdds_socket/keyed/type "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(public_socket_dds_tests PRIVATE fastdds_dsoftbus_public_socket_shim_fake fastdds_dsoftbus_host_provider_loop fastcdr)
    add_executable(public_socket_shared_dds_tests tests/public_socket_shared_dds_tests.cpp tests/FakePublicSocketLink.cpp
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSamplePubSubTypes.cxx
        experiments/fastdds_socket/keyed/type/DSoftBusKeyedSampleTypeObjectSupport.cxx)
    target_include_directories(public_socket_shared_dds_tests PRIVATE experiments/fastdds_socket/keyed/type "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(public_socket_shared_dds_tests PRIVATE fastdds_dsoftbus_public_socket_shim_fake fastdds_dsoftbus_host_provider_loop fastcdr)
    foreach(_shared_dds_case creator joiner disabled-link cycle-1 cycle-10 cycle-100)
        add_test(NAME dsoftbus.public_socket_shared_dds.${_shared_dds_case} COMMAND public_socket_shared_dds_tests ${_shared_dds_case})
        set_tests_properties(dsoftbus.public_socket_shared_dds.${_shared_dds_case} PROPERTIES TIMEOUT 900)
    endforeach()
    add_executable(public_socket_pair_contract_tests tests/public_socket_pair_contract_tests.cpp tests/FakePublicSocketLink.cpp)
    add_executable(public_socket_fake_sampling_tests tests/public_socket_fake_sampling_tests.cpp tests/FakePublicSocketLink.cpp)
    target_include_directories(public_socket_fake_sampling_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_compile_features(public_socket_fake_sampling_tests PRIVATE cxx_std_17)
    foreach(_sampling_case full limited sampled errors overflow transition disabled)
        add_test(NAME dsoftbus.fake_sampling.${_sampling_case} COMMAND public_socket_fake_sampling_tests ${_sampling_case})
        set_tests_properties(dsoftbus.fake_sampling.${_sampling_case} PROPERTIES TIMEOUT 20)
    endforeach()
    target_include_directories(public_socket_pair_contract_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(public_socket_pair_contract_tests PRIVATE fastdds_dsoftbus_public_socket_shim_fake fastdds_dsoftbus_host_provider_loop)
    add_test(NAME dsoftbus.public_socket_pair.half-close COMMAND public_socket_pair_contract_tests)
    set_tests_properties(dsoftbus.public_socket_pair.half-close PROPERTIES TIMEOUT 20)
    foreach(_public_dds_case normal-b-first normal-a-first disabled-link accepted-reject default)
        add_test(NAME dsoftbus.public_socket_dds.${_public_dds_case} COMMAND public_socket_dds_tests ${_public_dds_case})
        set_tests_properties(dsoftbus.public_socket_dds.${_public_dds_case} PROPERTIES TIMEOUT 45)
    endforeach()
    foreach(_accepted_case connected-window split-bind-window normal reject duplicate early-late stop-before-commit local-close borrow copy running repeat unknown negotiation2 callback-error first-error commit-window observed-all no-allocation-drain reopen-rollback-dual)
        add_test(NAME dsoftbus.public_socket_accepted.${_accepted_case} COMMAND public_socket_accepted_tests ${_accepted_case})
        set_tests_properties(dsoftbus.public_socket_accepted.${_accepted_case} PROPERTIES TIMEOUT 30)
    endforeach()
    target_include_directories(public_socket_shim_tests PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(public_socket_shim_tests PRIVATE fastdds_dsoftbus_public_socket_shim_fake fastdds_dsoftbus_host_provider_loop)
    if(MSVC)
        # These test executables define the intentionally throwing Fake C API.
        # Preserve C++ unwind and remove only the C-nothrow assumption.
        foreach(_fake_consumer public_socket_shim_tests public_socket_accepted_tests public_socket_dds_tests public_socket_pair_contract_tests)
            target_compile_options(${_fake_consumer} PRIVATE /EHsc-)
        endforeach()
    endif()

    foreach(_shim_case parameters lifetime receive early-bytes early-stop early-error foreign socket-error bind-error zero-handle send send-error send-positive send-throw send-reject send-paused repeat-close invalid entrance-identity default rollback-other observe-setup listen listen-error listen-stop)
        add_test(NAME dsoftbus.public_socket_shim.${_shim_case} COMMAND public_socket_shim_tests ${_shim_case})
        set_tests_properties(dsoftbus.public_socket_shim.${_shim_case} PROPERTIES TIMEOUT 20)
    endforeach()
endif()

if(BUILD_DSOFTBUS_PUBLIC_SOCKET_SHIM)
    if(NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        message(FATAL_ERROR "PUBLIC_SOCKET_TARGET_ONLY: use the declared OHOS toolchain")
    endif()
    if(NOT TARGET fastdds_dsoftbus_provider_adapter_phase0)
        message(FATAL_ERROR "Public Socket shim requires the existing ProviderAdapter")
    endif()
    set(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_LIBRARY "" CACHE FILEPATH "Declared target libsoftbus_client.z.so (compile/link audit only)")
    if(NOT IS_ABSOLUTE "${FASTDDS_DSOFTBUS_PUBLIC_SOCKET_LIBRARY}" OR NOT EXISTS "${FASTDDS_DSOFTBUS_PUBLIC_SOCKET_LIBRARY}")
        message(FATAL_ERROR "Declare an existing absolute FASTDDS_DSOFTBUS_PUBLIC_SOCKET_LIBRARY")
    endif()
    add_library(fastdds_dsoftbus_public_socket_api SHARED IMPORTED)
    set_target_properties(fastdds_dsoftbus_public_socket_api PROPERTIES IMPORTED_LOCATION "${FASTDDS_DSOFTBUS_PUBLIC_SOCKET_LIBRARY}")
    add_library(fastdds_dsoftbus_public_socket_shim STATIC src/PublicSocketBackend.cpp)
    target_include_directories(fastdds_dsoftbus_public_socket_shim PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport")
    target_link_libraries(fastdds_dsoftbus_public_socket_shim PUBLIC fastdds_dsoftbus_provider_adapter_phase0 PRIVATE fastdds_dsoftbus_public_socket_api)
    set_property(TARGET fastdds_dsoftbus_public_socket_shim PROPERTY INTERFACE_FASTDDS_DSOFTBUS_PUBLIC_SOCKET_VARIANT real)
    set_property(TARGET fastdds_dsoftbus_public_socket_shim APPEND PROPERTY COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_PUBLIC_SOCKET_VARIANT)
    add_executable(public_socket_link_audit probes/public_socket_link_audit.cpp)
    target_link_libraries(public_socket_link_audit PRIVATE fastdds_dsoftbus_public_socket_shim)
    # Own undefined calls must resolve. The declared platform DSO's transitive
    # platform dependencies are not available to this app toolchain link audit.
    # This does not establish loader/runtime compatibility and is never executed.
    target_link_options(public_socket_link_audit PRIVATE "-Wl,--no-undefined" "-Wl,--allow-shlib-undefined" "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/public_socket_link_audit.map")
endif()
include(cmake/PublicSocketRuntimeProbe.cmake)

include(cmake/PublicSocketDdsExperiment.cmake)

# Repository-local offline analyzers; no device operations are registered here.
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(FASTDDS_DSOFTBUS_RECEIPT_NODE NAMES node REQUIRED)
    add_test(NAME dsoftbus.public_socket_executor.flow
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_executor/flow_tests.py")
    set_tests_properties(dsoftbus.public_socket_executor.flow PROPERTIES TIMEOUT 90)
    foreach(_receipt_case runner-tests shared-control-tests shared-cycle-tests stream-boundary
            controller-close control-receipt.red full-cycles control-matrix exit-drain
            test_claim test_dispatch test_termination test_kill_error)
        add_test(NAME dsoftbus.public_socket_executor.${_receipt_case}
            COMMAND "${FASTDDS_DSOFTBUS_RECEIPT_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_executor/${_receipt_case}.cjs")
        set_tests_properties(dsoftbus.public_socket_executor.${_receipt_case} PROPERTIES TIMEOUT 45)
    endforeach()
    add_test(NAME dsoftbus.public_socket_executor.exit-drain-injected
        COMMAND "${FASTDDS_DSOFTBUS_RECEIPT_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_executor/exit-drain.cjs" --inject-close)
    set_tests_properties(dsoftbus.public_socket_executor.exit-drain-injected PROPERTIES TIMEOUT 20)
    if(WIN32)
        find_program(FASTDDS_DSOFTBUS_RECEIPT_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
        add_test(NAME dsoftbus.public_socket_executor.remote-receipt
            COMMAND "${FASTDDS_DSOFTBUS_RECEIPT_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_executor/remote-receipt.tests.ps1")
        set_tests_properties(dsoftbus.public_socket_executor.remote-receipt PROPERTIES TIMEOUT 20)
    endif()
    add_test(NAME dsoftbus.public_socket_analysis.cycles COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_cycle_analysis_tests.py")
    add_test(NAME dsoftbus.public_socket_analysis.send_cancellation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_send_cancellation_analysis_tests.py")
    add_test(NAME dsoftbus.public_socket_analysis.device_cycles COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_device_cycle_analysis_tests.py")
    if(TARGET public_socket_fake_sampling_tests)
        add_test(NAME dsoftbus.fake_sampling.parity COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_sampling_parity.py" "$<TARGET_FILE:public_socket_fake_sampling_tests>")
    endif()
    foreach(_public_analysis dds probe load_origin)
        add_test(NAME dsoftbus.public_socket_analysis.${_public_analysis}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_${_public_analysis}_analysis_tests.py")
    endforeach()
endif()

if(BUILD_TESTING AND BUILD_PUBLIC_SOCKET_BOUNDED_REGRESSION)
    add_test(NAME dsoftbus.public_socket_analysis.regression COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_regression_analysis_tests.py")
    add_test(NAME dsoftbus.public_socket_analysis.decisions COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_decision_analysis_tests.py")
    add_test(NAME dsoftbus.public_socket_analysis.shared COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/public_socket_shared_analysis_tests.py")
endif()

include(cmake/FastDDSDSoftBusTransportLibraryDelivery.cmake)

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME dsoftbus.naming.configuration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/naming_configuration_tests.py"
                --source "${CMAKE_CURRENT_SOURCE_DIR}" --work "${CMAKE_CURRENT_BINARY_DIR}/naming-options"
                --fastdds-source "${FASTDDS_SOURCE_DIR}")
        set_tests_properties(dsoftbus.naming.configuration PROPERTIES TIMEOUT 180)
    endif()
endif()
