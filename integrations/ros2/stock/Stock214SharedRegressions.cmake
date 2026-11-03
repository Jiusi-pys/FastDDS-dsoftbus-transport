# Original shared driver against the same consistent stock 2.14 Host variant.
if(NOT TARGET stock_214_host_backend_testing)
    message(FATAL_ERROR "Stock214 shared regressions require the consistent HostBackendTesting source assembly")
endif()
add_executable(stock_214_shared_channel_tests "${_repo}/tests/host_shared_channel_tests.cpp")
target_link_libraries(stock_214_shared_channel_tests PRIVATE stock_214_host_backend_testing)
foreach(_shared_case send-classification send-control-ledger cancel-taken cancel-admitted shared creator
        controller-released reattach queued running foreign foreign-peer-records
        foreign-peer-bytes foreign-global-records foreign-global-bytes foreign-record-bound-legal old-snapshot budget
        open-close-race concurrent-first concurrent-close copying own-lease candidate-order
        capture-accounting pinned-close admitted rollback sending join-last
        egress-settled egress-queued egress-old-resource pump-authority budget-two-views budget-ceiling)
    add_test(NAME stock_transport.214.shared.${_shared_case}
        COMMAND stock_214_shared_channel_tests ${_shared_case})
    set_tests_properties(stock_transport.214.shared.${_shared_case} PROPERTIES TIMEOUT 20)
endforeach()
add_custom_target(stock_214_shared_regressions DEPENDS stock_214_shared_channel_tests)
