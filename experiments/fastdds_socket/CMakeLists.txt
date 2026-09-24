# This directory is reached only through the root's default-OFF experiment gate.
if(NOT BUILD_FASTDDS_DSOFTBUS_EXPERIMENT)
    message(FATAL_ERROR "The Fast DDS Socket experiment requires explicit opt-in")
endif()
if(NOT MDDS_FASTDDS_MODE STREQUAL "CONFIG")
    message(FATAL_ERROR "The experiment requires MDDS_FASTDDS_MODE=CONFIG and real Fast DDS libraries")
endif()
if(NOT TARGET mdds_fastdds_binding)
    include("${PROJECT_SOURCE_DIR}/cmake/FastDDSBinding.cmake")
endif()
if(NOT fastdds_VERSION VERSION_EQUAL "3.6.2.0")
    message(FATAL_ERROR "The experiment requires real Fast DDS 3.6.2.0")
endif()
option(MDDS_EXPERIMENT_NATIVE "Build the unqualified OpenHarmony experiment program" OFF)
option(MDDS_EXPERIMENT_RX_DROP "Enable the dedicated, controlled receiver-boundary loss experiment" OFF)
set(MDDS_EXPERIMENT_TOKEN_BRIDGE "" CACHE FILEPATH "Platform-ABI C/POD token verification bridge")
find_package(Threads REQUIRED)

add_library(experimental_dsoftbus_transport STATIC
    ExperimentalTransport.cpp PerformanceObservation.cpp "${PROJECT_SOURCE_DIR}/src/V1Address.cpp"
    "${PROJECT_SOURCE_DIR}/src/EndpointDirectory.cpp"
    "${PROJECT_SOURCE_DIR}/src/NativeOwnerTable.cpp"
    "${PROJECT_SOURCE_DIR}/src/SendQueue.cpp"
    "${PROJECT_SOURCE_DIR}/src/RuntimeSendPath.cpp"
    "${PROJECT_SOURCE_DIR}/src/DSoftBusSenderResource.cpp"
    "${PROJECT_SOURCE_DIR}/src/InputChannels.cpp"
    "${PROJECT_SOURCE_DIR}/src/ApplicationAttachments.cpp"
    "${PROJECT_SOURCE_DIR}/src/NativeSendExecution.cpp"
    "${PROJECT_SOURCE_DIR}/src/ProcessReceivePath.cpp"
    "${PROJECT_SOURCE_DIR}/src/ProcessCoordinator.cpp")
target_compile_features(experimental_dsoftbus_transport PUBLIC cxx_std_17)
target_compile_definitions(experimental_dsoftbus_transport PUBLIC MDDS_UNQUALIFIED_EXPERIMENT=1)
if(MDDS_EXPERIMENT_RX_DROP)
    target_compile_definitions(experimental_dsoftbus_transport PUBLIC MDDS_EXPERIMENT_RX_DROP=1)
endif()
target_include_directories(experimental_dsoftbus_transport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/include"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/transport"
    "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/common")
target_link_libraries(experimental_dsoftbus_transport PUBLIC mdds_fastdds_binding Threads::Threads)

add_library(experiment_large_sample_type STATIC
    large_sample/ExperimentLargeSamplePubSubTypes.cxx
    large_sample/ExperimentLargeSampleTypeObjectSupport.cxx)
target_compile_features(experiment_large_sample_type PUBLIC cxx_std_17)
target_include_directories(experiment_large_sample_type PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/large_sample")
target_link_libraries(experiment_large_sample_type PUBLIC mdds_fastdds_binding fastcdr)

# The new DDS matrix is confined to this existing default-OFF experiment gate.
add_subdirectory(matrix)
add_subdirectory(keyed)
add_subdirectory(perf)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(performance_native_config_tests performance_native_config_tests.cpp)
    target_link_libraries(performance_native_config_tests PRIVATE experimental_dsoftbus_transport)
    add_test(NAME experiment.performance_native_config COMMAND performance_native_config_tests)
    add_executable(performance_observation_tests performance_observation_tests.cpp)
    target_link_libraries(performance_observation_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_mode IN ITEMS default trace measure counter overflow)
        add_test(NAME experiment.performance_observation_${_mode} COMMAND performance_observation_tests ${_mode})
        set_tests_properties(experiment.performance_observation_${_mode} PROPERTIES TIMEOUT 30)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET performance_observation_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
            "$<TARGET_FILE_DIR:performance_observation_tests>")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/MultiPeerTests.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/MatrixNativeTests.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/KeyedNativeTests.cmake")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME experiment.performance_analyzer COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/dds_performance_analysis_tests.py")
    set_tests_properties(experiment.performance_analyzer PROPERTIES TIMEOUT 60)
    add_test(NAME experiment.keyed_analyzer COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/dds_keyed_analysis_tests.py")
    set_tests_properties(experiment.keyed_analyzer PROPERTIES TIMEOUT 60)
    add_test(NAME experiment.rx_admission_analyzer COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/rx_admission_analysis_tests.py")
    set_tests_properties(experiment.rx_admission_analyzer PROPERTIES TIMEOUT 15)
    add_executable(experiment_core_rx_admission_diagnostics_tests "${PROJECT_SOURCE_DIR}/tests/core_rx_admission_diagnostics_tests.cpp")
    target_link_libraries(experiment_core_rx_admission_diagnostics_tests PRIVATE mdds_dsoftbus_process)
    add_test(NAME experiment.core_rx_admission_diagnostics COMMAND experiment_core_rx_admission_diagnostics_tests)
    set_tests_properties(experiment.core_rx_admission_diagnostics PROPERTIES TIMEOUT 20)
    add_executable(experiment_rx_admission_diagnostics_tests rx_admission_diagnostics_tests.cpp)
    target_link_libraries(experiment_rx_admission_diagnostics_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_rx_case IN ITEMS entry overflow native-commit closing capacity-records capacity-bytes capacity-copying)
        add_test(NAME experiment.rx_admission_${_rx_case} COMMAND experiment_rx_admission_diagnostics_tests ${_rx_case})
        set_tests_properties(experiment.rx_admission_${_rx_case} PROPERTIES TIMEOUT 20)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_rx_admission_diagnostics_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_rx_admission_diagnostics_tests>")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/HotTests.cmake")
    add_executable(experiment_runtime_lifecycle_tests runtime_lifecycle_tests.cpp)
    target_link_libraries(experiment_runtime_lifecycle_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_runtime_case IN ITEMS L1 L2 L3 L4 L5 L6 L7 L8 L9 L10 L11 L12 prepare-rollback allocate-rollback acquire-closing final-result registry-last-release endpoint-cap setup-create setup-bind repeat100 submit-drain)
        add_test(NAME experiment.runtime_${_runtime_case} COMMAND experiment_runtime_lifecycle_tests ${_runtime_case})
        set_tests_properties(experiment.runtime_${_runtime_case} PROPERTIES TIMEOUT 30)
    endforeach()
    set_tests_properties(experiment.runtime_registry-last-release PROPERTIES TIMEOUT 10)
    if(WIN32)
        add_custom_command(TARGET experiment_runtime_lifecycle_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_runtime_lifecycle_tests>")
    endif()
    add_executable(experiment_dual_validation_tests dual_validation_tests.cpp)
    target_compile_features(experiment_dual_validation_tests PRIVATE cxx_std_17)
    target_link_libraries(experiment_dual_validation_tests PRIVATE mdds_fastdds_binding)
    if(WIN32)
        add_custom_command(TARGET experiment_dual_validation_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_dual_validation_tests>")
    endif()
    foreach(_validation_case IN ITEMS D1 D2 D3 D4 D5 D6 D7 D8 D9 S1 S2 S3 S4 S5 S6 S7 S8 S9 S10 X1 X2 X3 X4)
        add_test(NAME experiment.dual_validation_${_validation_case} COMMAND experiment_dual_validation_tests ${_validation_case})
        set_tests_properties(experiment.dual_validation_${_validation_case} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(experiment_dual_matching_tests dual_matching_tests.cpp)
    target_link_libraries(experiment_dual_matching_tests PRIVATE experiment_large_sample_type)
    foreach(_matching_case IN ITEMS control isolated fixture)
        add_test(NAME experiment.dual_matching_${_matching_case} COMMAND experiment_dual_matching_tests ${_matching_case})
        set_tests_properties(experiment.dual_matching_${_matching_case} PROPERTIES TIMEOUT 15)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_dual_matching_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_dual_matching_tests>")
    endif()
    add_executable(experiment_multi_instance_tests multi_instance_tests.cpp)
    target_link_libraries(experiment_multi_instance_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_multi_case IN ITEMS second-instance conflict lifecycle wrong-entry stop-isolation tx-lifetime rx-lifetime)
        add_test(NAME experiment.multi_${_multi_case} COMMAND experiment_multi_instance_tests ${_multi_case})
        set_tests_properties(experiment.multi_${_multi_case} PROPERTIES TIMEOUT 30)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_multi_instance_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_multi_instance_tests>")
    endif()
    add_executable(experiment_dual_scenario_checks_tests dual_scenario_checks_tests.cpp)
    target_compile_features(experiment_dual_scenario_checks_tests PRIVATE cxx_std_17)
    add_test(NAME experiment.dual_scenario_checks COMMAND experiment_dual_scenario_checks_tests)
    set_tests_properties(experiment.dual_scenario_checks PROPERTIES TIMEOUT 10)
    add_executable(experiment_rx_drop_tests rx_drop_tests.cpp)
    target_link_libraries(experiment_rx_drop_tests PRIVATE experimental_dsoftbus_transport)
    if(MDDS_EXPERIMENT_RX_DROP)
        foreach(_drop_case IN ITEMS hit miss runtime-off)
            add_test(NAME experiment.rx_drop_${_drop_case} COMMAND experiment_rx_drop_tests ${_drop_case})
            set_tests_properties(experiment.rx_drop_${_drop_case} PROPERTIES TIMEOUT 12)
        endforeach()
    else()
        add_test(NAME experiment.rx_drop_gate COMMAND experiment_rx_drop_tests)
        set_tests_properties(experiment.rx_drop_gate PROPERTIES TIMEOUT 12)
    endif()
    if(WIN32)
        add_custom_command(TARGET experiment_rx_drop_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_rx_drop_tests>")
    endif()
    add_executable(experiment_submit_exception_tests submit_exception_tests.cpp)
    target_link_libraries(experiment_submit_exception_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_submit_case IN ITEMS pending settled)
        add_test(NAME experiment.submit_exception_${_submit_case} COMMAND experiment_submit_exception_tests ${_submit_case})
        set_tests_properties(experiment.submit_exception_${_submit_case} PROPERTIES TIMEOUT 15)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_submit_exception_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_submit_exception_tests>")
    endif()
    add_executable(experiment_loss_scenario_checks_tests loss_scenario_checks_tests.cpp)
    target_compile_features(experiment_loss_scenario_checks_tests PRIVATE cxx_std_17)
    add_test(NAME experiment.loss_scenario_checks COMMAND experiment_loss_scenario_checks_tests)
    set_tests_properties(experiment.loss_scenario_checks PROPERTIES TIMEOUT 10)
    add_executable(experiment_large_sample_contract_tests large_sample_contract_tests.cpp)
    target_link_libraries(experiment_large_sample_contract_tests PRIVATE experiment_large_sample_type)
    add_test(NAME experiment.large_sample_contract COMMAND experiment_large_sample_contract_tests)
    set_tests_properties(experiment.large_sample_contract PROPERTIES TIMEOUT 15)
    if(WIN32)
        add_custom_command(TARGET experiment_large_sample_contract_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_large_sample_contract_tests>")
    endif()
    add_executable(experiment_rtps_observer_tests rtps_observer_tests.cpp)
    target_link_libraries(experiment_rtps_observer_tests PRIVATE experimental_dsoftbus_transport)
    add_test(NAME experiment.rtps_observer COMMAND experiment_rtps_observer_tests)
    set_tests_properties(experiment.rtps_observer PROPERTIES TIMEOUT 10)
    if(WIN32)
        add_custom_command(TARGET experiment_rtps_observer_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_rtps_observer_tests>")
    endif()
    add_executable(experiment_lifecycle_hardening_tests lifecycle_hardening_tests.cpp)
    target_link_libraries(experiment_lifecycle_hardening_tests PRIVATE experimental_dsoftbus_transport)
    foreach(_case IN ITEMS bind-shutdown bind-error stop-send reservation reserved-without-bind reserved-terminal lifetime-tx lifetime-rx)
        add_test(NAME experiment.hardening_${_case} COMMAND experiment_lifecycle_hardening_tests ${_case})
        set_tests_properties(experiment.hardening_${_case} PROPERTIES TIMEOUT 15)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_lifecycle_hardening_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_lifecycle_hardening_tests>")
    endif()
    add_executable(experiment_listener_policy_tests listener_policy_tests.cpp)
    target_link_libraries(experiment_listener_policy_tests PRIVATE mdds_fastdds_binding)
    target_compile_features(experiment_listener_policy_tests PRIVATE cxx_std_17)
    add_test(NAME experiment.listener_policy COMMAND experiment_listener_policy_tests)
    set_tests_properties(experiment.listener_policy PROPERTIES TIMEOUT 10)
    add_executable(experiment_xml_isolation_tests xml_isolation_tests.cpp)
    target_link_libraries(experiment_xml_isolation_tests PRIVATE mdds_fastdds_binding)
    target_compile_features(experiment_xml_isolation_tests PRIVATE cxx_std_17)
    foreach(_xml_mode IN ITEMS control skip)
        add_test(NAME experiment.xml_${_xml_mode} COMMAND experiment_xml_isolation_tests ${_xml_mode})
        set_tests_properties(experiment.xml_${_xml_mode} PROPERTIES TIMEOUT 10
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/xml_fixture")
    endforeach()
    if(WIN32)
        add_custom_command(TARGET experiment_xml_isolation_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experiment_xml_isolation_tests>")
    endif()
    add_executable(scenario_checks_tests scenario_checks_tests.cpp)
    target_compile_features(scenario_checks_tests PRIVATE cxx_std_17)
    add_test(NAME experiment.scenario_checks COMMAND scenario_checks_tests)
    set_tests_properties(experiment.scenario_checks PROPERTIES TIMEOUT 10)
    add_executable(experimental_transport_contract_tests transport_contract_tests.cpp)
    target_link_libraries(experimental_transport_contract_tests PRIVATE experimental_dsoftbus_transport)
    add_test(NAME experiment.transport_contract COMMAND experimental_transport_contract_tests)
    add_test(NAME experiment.transport_late_bind_close COMMAND experimental_transport_contract_tests --late-bind-close)
    add_test(NAME experiment.native_service_name COMMAND experimental_transport_contract_tests --native-service-name)
    set_tests_properties(experiment.native_service_name PROPERTIES TIMEOUT 30)
    set_tests_properties(experiment.transport_late_bind_close PROPERTIES TIMEOUT 30)
    set_tests_properties(experiment.transport_contract PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_custom_command(TARGET experimental_transport_contract_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:experimental_transport_contract_tests>")
    endif()
endif()

if(MDDS_EXPERIMENT_NATIVE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
        message(FATAL_ERROR "Native experiment program requires CMAKE_SYSTEM_NAME=OHOS")
    endif()
    if(NOT IS_ABSOLUTE "${MDDS_EXPERIMENT_TOKEN_BRIDGE}" OR
            NOT EXISTS "${MDDS_EXPERIMENT_TOKEN_BRIDGE}")
        message(FATAL_ERROR "Native experiment requires an explicit existing platform C/POD token bridge")
    endif()
    add_library(mdds_experiment_token_bridge SHARED IMPORTED)
    set_target_properties(mdds_experiment_token_bridge PROPERTIES
        IMPORTED_LOCATION "${MDDS_EXPERIMENT_TOKEN_BRIDGE}")
    add_executable(fastdds_socket_experiment main.cpp
        "${PROJECT_SOURCE_DIR}/probes/token_first_entry.cpp"
        "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world/HelloWorldPubSubTypes.cxx"
        "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world/HelloWorldTypeObjectSupport.cxx")
    target_include_directories(fastdds_socket_experiment PRIVATE
        "${PROJECT_SOURCE_DIR}/probes"
        "${DSOFTBUS_SOURCE_DIR}/interfaces/kits/bus_center"
        "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world")
    target_compile_definitions(fastdds_socket_experiment PRIVATE MDDS_EXPERIMENT_NATIVE_ACKNOWLEDGED=1)
    target_link_libraries(fastdds_socket_experiment PRIVATE
        experimental_dsoftbus_transport experiment_large_sample_type experiment_matrix_fixture experiment_keyed_fixture experiment_perf_fixture mdds_experiment_token_bridge fastcdr ${CMAKE_DL_LIBS})
    set_target_properties(fastdds_socket_experiment PROPERTIES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
