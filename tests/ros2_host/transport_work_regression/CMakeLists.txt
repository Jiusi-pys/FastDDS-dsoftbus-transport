cmake_minimum_required(VERSION 3.22)
project(TransportWorkComponentRegression LANGUAGES CXX)
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Host component regression only")
endif()
get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(FASTDDS_SOURCE_DIR "" CACHE PATH "Read-only Fast DDS 3.6 source-interface probe headers")
set(DSOFTBUS_SOURCE_DIR "" CACHE PATH "Read-only official Socket headers")
if(NOT EXISTS "${FASTDDS_SOURCE_DIR}/src/cpp/rtps/transport/TransportInterface.cpp" OR
        NOT EXISTS "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport/socket.h")
    message(FATAL_ERROR "Both explicit read-only source roots required")
endif()
# This is the former 3.6 component interface, not a complete DDS library and not
# stock 2.14 integration. Never load a second DDS DLL into the stock API tests.
include("${_repo}/cmake/ProbeFastddsConfig.cmake")
find_package(Threads REQUIRED)
include("${_repo}/integrations/ros2/RuntimeSources.cmake")
set(_components ${_files})
list(FILTER _components EXCLUDE REGEX "/(PublicSocketBackend|ExperimentalRuntime)\\.cpp$")
add_library(work_regression_core STATIC ${_components}
    "${FASTDDS_SOURCE_DIR}/src/cpp/rtps/transport/TransportInterface.cpp")
target_compile_features(work_regression_core PUBLIC cxx_std_17)
target_compile_definitions(work_regression_core PUBLIC FASTDDS_STATIC_LINK FASTDDS_NO_LIB
    FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING=1 NOMINMAX)
target_include_directories(work_regression_core PUBLIC "${_repo}/include" "${_repo}/tests"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${FASTDDS_SOURCE_DIR}/include")
target_link_libraries(work_regression_core PUBLIC Threads::Threads)
add_library(work_regression_socket STATIC "${_repo}/src/PublicSocketBackend.cpp" "${_repo}/tests/FakeNodeDirectory.cpp")
target_compile_definitions(work_regression_socket PUBLIC FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK=1)
target_include_directories(work_regression_socket PUBLIC "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
target_link_libraries(work_regression_socket PUBLIC work_regression_core)
if(MSVC)
    target_compile_options(work_regression_socket PRIVATE /EHsc-)
endif()
foreach(_test send_queue runtime_send_path application_attachments native_send_execution
        process_receive process_coordinator host_provider_loop host_shared_channel host_input_close
        runtime_shutdown_observer public_socket_shim public_socket_accepted public_socket_reaccept)
    add_executable(${_test}_tests "${_repo}/tests/${_test}_tests.cpp")
    if(_test MATCHES "^public_socket_")
        target_link_libraries(${_test}_tests PRIVATE work_regression_socket)
        if(MSVC)
            target_compile_options(${_test}_tests PRIVATE /EHsc-)
        endif()
    else()
        target_link_libraries(${_test}_tests PRIVATE work_regression_core)
    endif()
endforeach()

# Separate real-shim assembly over Fake C symbols, never HOST_BACKEND_TESTING.
add_library(work_regression_experimental STATIC ${_files}
    "${FASTDDS_SOURCE_DIR}/src/cpp/rtps/transport/TransportInterface.cpp"
    "${_repo}/tests/FakePublicSocketLink.cpp" "${_repo}/tests/FakeNodeDirectory.cpp"
    "${_repo}/integrations/ros2/Observation.cpp")
target_compile_features(work_regression_experimental PUBLIC cxx_std_17)
target_compile_definitions(work_regression_experimental PUBLIC FASTDDS_STATIC_LINK FASTDDS_NO_LIB NOMINMAX
    FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1
    PRIVATE FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1)
target_include_directories(work_regression_experimental PUBLIC "${_repo}/include" "${_repo}/tests"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${FASTDDS_SOURCE_DIR}/include"
    PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
target_link_libraries(work_regression_experimental PUBLIC Threads::Threads)
add_executable(experimental_runtime_tests "${_repo}/tests/experimental_runtime_tests.cpp")
target_link_libraries(experimental_runtime_tests PRIVATE work_regression_experimental)
enable_testing()
add_library(work_regression_nohandle STATIC ${_files}
    "${FASTDDS_SOURCE_DIR}/src/cpp/rtps/transport/TransportInterface.cpp"
    "${_repo}/tests/FakeNodeDirectory.cpp" "${_repo}/integrations/ros2/Observation.cpp")
target_compile_features(work_regression_nohandle PUBLIC cxx_std_17)
target_compile_definitions(work_regression_nohandle PUBLIC FASTDDS_STATIC_LINK FASTDDS_NO_LIB NOMINMAX
    FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE=1 FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT=1)
target_include_directories(work_regression_nohandle PUBLIC "${_repo}/include" "${_repo}/tests"
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${FASTDDS_SOURCE_DIR}/include"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
target_link_libraries(work_regression_nohandle PUBLIC Threads::Threads)
add_executable(public_socket_nohandle_tests "${_repo}/tests/public_socket_nohandle_tests.cpp")
target_link_libraries(public_socket_nohandle_tests PRIVATE work_regression_nohandle)
if(MSVC)
    target_compile_options(work_regression_nohandle PRIVATE /EHsc-)
    target_compile_options(public_socket_nohandle_tests PRIVATE /EHsc-)
endif()
foreach(_case negative-distinct negative-same unknown listen-retained bind-retained synchronous old-entry concurrent generic default-public promotion-failure)
    add_test(NAME dsoftbus.public_socket_nohandle.${_case} COMMAND public_socket_nohandle_tests ${_case})
    set_tests_properties(dsoftbus.public_socket_nohandle.${_case} PROPERTIES TIMEOUT 25)
endforeach()
foreach(_test send_queue runtime_send_path application_attachments native_send_execution process_receive process_coordinator)
    add_test(NAME component.${_test} COMMAND ${_test}_tests)
    set_tests_properties(component.${_test} PROPERTIES TIMEOUT 25)
endforeach()
add_test(NAME component.experimental_runtime.baseline COMMAND experimental_runtime_tests)
set_tests_properties(component.experimental_runtime.baseline PROPERTIES TIMEOUT 25)
# Preserve the original shorter watchdogs; other new aggregate entries retain
# 25 seconds and scenario entries below keep their original per-case limits.
set_tests_properties(component.send_queue component.runtime_send_path PROPERTIES TIMEOUT 15)
set_tests_properties(component.application_attachments PROPERTIES TIMEOUT 20)
foreach(_case gate refusal ownership throw reentrant-lifetime service-scope service-reject identity reaccept-real-rejected)
    add_test(NAME component.experimental_runtime.${_case} COMMAND experimental_runtime_tests --${_case})
    set_tests_properties(component.experimental_runtime.${_case} PROPERTIES TIMEOUT 25)
endforeach()

# Existing scenario registration copied verbatim from root CMakeLists.
    foreach(_shutdown_case once throw nested shared active)
            add_test(NAME dsoftbus.runtime_shutdown_observer.${_shutdown_case} COMMAND runtime_shutdown_observer_tests ${_shutdown_case})
            set_tests_properties(dsoftbus.runtime_shutdown_observer.${_shutdown_case} PROPERTIES TIMEOUT 30)
        endforeach()
    foreach(_shared_case send-classification send-control-ledger cancel-taken cancel-admitted shared creator controller-released reattach queued running foreign foreign-peer-records foreign-peer-bytes foreign-global-records foreign-global-bytes foreign-record-bound-legal old-snapshot budget open-close-race concurrent-first concurrent-close copying own-lease candidate-order capture-accounting pinned-close admitted rollback sending join-last egress-settled egress-queued egress-old-resource pump-authority budget-two-views budget-ceiling)
            add_test(NAME dsoftbus.host_shared_channel.${_shared_case} COMMAND host_shared_channel_tests ${_shared_case})
            set_tests_properties(dsoftbus.host_shared_channel.${_shared_case} PROPERTIES TIMEOUT 20)
        endforeach()
    foreach(_close_case basic destroy queued copy running sending gap same-address reopen-failure locators fault allocation-sweep callback-control callback-during-close)
            add_test(NAME dsoftbus.host_input_close.${_close_case} COMMAND host_input_close_tests ${_close_case})
            set_tests_properties(dsoftbus.host_input_close.${_close_case} PROPERTIES TIMEOUT 20)
        endforeach()
    foreach(_loop_case default loop preopen input-close queued paused-send paused-receiver late error
                open-exception open-allocation open-racing-shutdown backend-size-bound backend-size-above backend-size-unspecified observe-lifetime shared-init startup-allocation setup-cleanup-failure receive-limit)
            add_test(NAME dsoftbus.host_provider_loop.${_loop_case} COMMAND host_provider_loop_tests ${_loop_case})
            set_tests_properties(dsoftbus.host_provider_loop.${_loop_case} PROPERTIES TIMEOUT 20)
        endforeach()
    foreach(_reaccept_case default fresh-old reused-ids wrong-peer candidates capacity old-send entered-send entered-receiver late-create close-once ambiguous-first failed-dial-accept entered-copy close-bind close-publication local-end local-end-send local-end-accepted replacement-gate callback-before-local local-shutdown-exception)
        add_test(NAME dsoftbus.public_socket_reaccept.${_reaccept_case} COMMAND public_socket_reaccept_tests ${_reaccept_case})
        set_tests_properties(dsoftbus.public_socket_reaccept.${_reaccept_case} PROPERTIES TIMEOUT 20)
    endforeach()
    foreach(_route_case route-offline route-entered route-new route-same route-query-fail route-identity-fail route-missing route-key-fail route-ambiguous route-late-resolve route-late-bind route-close route-unrelated-hint route-reg-close route-offline-no-allocation route-failure-return route-resolver-reentry route-reg-failed-close route-resolution-collision)
        add_test(NAME dsoftbus.route_lifecycle.${_route_case} COMMAND public_socket_reaccept_tests ${_route_case})
        set_tests_properties(dsoftbus.route_lifecycle.${_route_case} PROPERTIES TIMEOUT 20)
    endforeach()
    foreach(_accepted_case connected-window split-bind-window normal reject duplicate early-late stop-before-commit local-close borrow copy running repeat unknown negotiation2 callback-error first-error commit-window observed-all no-allocation-drain reopen-rollback-dual)
        add_test(NAME dsoftbus.public_socket_accepted.${_accepted_case} COMMAND public_socket_accepted_tests ${_accepted_case})
        set_tests_properties(dsoftbus.public_socket_accepted.${_accepted_case} PROPERTIES TIMEOUT 30)
    endforeach()
    foreach(_shim_case parameters lifetime receive early-bytes early-stop early-error foreign socket-error bind-error zero-handle send send-error send-positive send-throw send-reject send-paused repeat-close invalid entrance-identity default rollback-other observe-setup listen listen-error listen-stop)
        add_test(NAME dsoftbus.public_socket_shim.${_shim_case} COMMAND public_socket_shim_tests ${_shim_case})
        set_tests_properties(dsoftbus.public_socket_shim.${_shim_case} PROPERTIES TIMEOUT 20)
    endforeach()
