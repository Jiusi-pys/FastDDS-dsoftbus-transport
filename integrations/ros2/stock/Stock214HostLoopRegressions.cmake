# Native Host-only 2.14 entry: compile the injected facade consistently.
# Do not reuse the separately compiled DDS-experiment/probe runtime archive.
set(_stock_214_host_loop_sources ${_files})
list(REMOVE_ITEM _stock_214_host_loop_sources "${_transport_root}/src/ExperimentalRuntime.cpp")
add_library(stock_214_host_backend_testing STATIC ${_stock_214_host_loop_sources}
    "${_repo}/tests/FakePublicSocketLink.cpp" "${_repo}/tests/FakeNodeDirectory.cpp")
target_compile_features(stock_214_host_backend_testing PUBLIC cxx_std_17)
target_compile_definitions(stock_214_host_backend_testing PUBLIC
    FASTDDS_DSOFTBUS_FASTDDS_214=1 FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING=1
    FASTDDS_DSOFTBUS_PUBLIC_SOCKET_HOST_C_SEAM=1)
target_include_directories(stock_214_host_backend_testing PUBLIC "${_repo}/include" "${_repo}/tests"
    PRIVATE "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center" "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
target_link_libraries(stock_214_host_backend_testing PUBLIC fastrtps fastcdr Threads::Threads)
set_property(TARGET stock_214_host_backend_testing PROPERTY
    INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT stock-214-host-backend-testing)
set_property(TARGET stock_214_host_backend_testing APPEND PROPERTY
    COMPATIBLE_INTERFACE_STRING FASTDDS_DSOFTBUS_FACADE_VARIANT)

add_executable(stock_214_host_provider_loop_tests "${_repo}/tests/host_provider_loop_tests.cpp")
target_link_libraries(stock_214_host_provider_loop_tests PRIVATE stock_214_host_backend_testing)
foreach(_loop_case default loop preopen input-close queued paused-send paused-receiver late error
        open-exception open-allocation open-racing-shutdown backend-size-bound backend-size-above
        backend-size-unspecified observe-lifetime shared-init startup-allocation setup-cleanup-failure receive-limit)
    add_test(NAME stock_transport.214.host_loop.${_loop_case}
        COMMAND stock_214_host_provider_loop_tests ${_loop_case})
    set_tests_properties(stock_transport.214.host_loop.${_loop_case} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(stock_214_host_input_close_tests "${_repo}/tests/host_input_close_tests.cpp")
target_link_libraries(stock_214_host_input_close_tests PRIVATE stock_214_host_backend_testing)
foreach(_close_case basic destroy queued copy running sending gap same-address reopen-failure locators
        fault allocation-sweep callback-control callback-during-close)
    add_test(NAME stock_transport.214.input_close.${_close_case}
        COMMAND stock_214_host_input_close_tests ${_close_case})
    set_tests_properties(stock_transport.214.input_close.${_close_case} PROPERTIES TIMEOUT 20)
endforeach()
add_custom_target(stock_214_host_loop_regressions
    DEPENDS stock_214_host_provider_loop_tests stock_214_host_input_close_tests)
