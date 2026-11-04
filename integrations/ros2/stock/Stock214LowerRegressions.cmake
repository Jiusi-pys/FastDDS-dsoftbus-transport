# Native Host-only entry; the including project already requires fastrtps 2.14.5 EXACT.
# Reuse original behavioral drivers/oracles; do not alias the legacy fastdds target.
if(NOT TARGET stock_transport_host_fake)
    message(FATAL_ERROR "Stock214 lower regressions require the stock Host runtime assembly")
endif()
add_executable(stock_214_input_channels_tests "${_repo}/tests/input_channels_tests.cpp")
target_link_libraries(stock_214_input_channels_tests PRIVATE stock_transport_host_fake)
add_test(NAME stock_transport.214.input_channels COMMAND stock_214_input_channels_tests)
set_tests_properties(stock_transport.214.input_channels PROPERTIES TIMEOUT 15)

add_executable(stock_214_provider_adapter_tests "${_repo}/tests/provider_adapter_tests.cpp")
target_link_libraries(stock_214_provider_adapter_tests PRIVATE stock_transport_host_fake)
foreach(_provider_case decision-branches P0 P1 P2 P3 P4 P5 P6 P7 early-stop provider-throw
        listen create-stop create-foreign create-conflict shutdown-failure ingress-allocation setup-observe)
    add_test(NAME stock_transport.214.provider_phase0.${_provider_case}
        COMMAND stock_214_provider_adapter_tests ${_provider_case})
    set_tests_properties(stock_transport.214.provider_phase0.${_provider_case} PROPERTIES TIMEOUT 15)
endforeach()
add_custom_target(stock_214_lower_regressions
    DEPENDS stock_214_input_channels_tests stock_214_provider_adapter_tests)
