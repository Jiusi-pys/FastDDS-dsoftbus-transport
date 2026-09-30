add_library(experiment_perf_fixture STATIC PortableFixture.cpp Measurement.cpp Resources.cpp)
target_compile_features(experiment_perf_fixture PUBLIC cxx_std_17)
target_include_directories(experiment_perf_fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(experiment_perf_fixture PUBLIC experimental_dsoftbus_transport experiment_large_sample_type)
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(perf_reference reference_main.cpp)
    target_link_libraries(perf_reference PRIVATE experiment_perf_fixture)
    if(WIN32)
        add_custom_command(TARGET perf_reference POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>" "$<TARGET_FILE_DIR:perf_reference>")
    endif()
    if(BUILD_TESTING)
        find_program(PERF_NODE_EXECUTABLE NAMES node REQUIRED)
        foreach(_mode IN ITEMS TRACE MEASURE)
            add_test(NAME experiment.perf_reference_${_mode} COMMAND "${PERF_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/reference_harness.cjs" "$<TARGET_FILE:perf_reference>" "${_mode}"
                "--root=${CMAKE_BINARY_DIR}/perf-reference" HOST_CONTRACT 1)
            set_tests_properties(experiment.perf_reference_${_mode} PROPERTIES TIMEOUT 225 RUN_SERIAL TRUE)
        endforeach()
        target_compile_definitions(experiment_perf_fixture PUBLIC FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS=1)
        add_executable(perf_fixture_tests fixture_tests.cpp)
        target_link_libraries(perf_fixture_tests PRIVATE experiment_perf_fixture)
        foreach(_contract IN ITEMS wrong-length real-echo cleanup serialization late-original no-first future-echo expired-slot)
            add_test(NAME experiment.perf_fixture_${_contract} COMMAND perf_fixture_tests "${_contract}" 28170)
            set_tests_properties(experiment.perf_fixture_${_contract} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        endforeach()
        foreach(_contract IN ITEMS phase-order
                deadline-a-request-before deadline-a-request-equal deadline-a-request-after
                deadline-a-phase-before deadline-a-phase-equal deadline-a-phase-after
                deadline-a-global-before deadline-a-global-equal deadline-a-global-after
                deadline-b-phase-before deadline-b-phase-equal deadline-b-phase-after deadline-b-phase-delay deadline-b-global-delay
                deadline-load-end-equal deadline-load-first-equal deadline-load-observation-equal deadline-write-global-equal)
            add_test(NAME experiment.perf_fixture_${_contract} COMMAND perf_fixture_tests "${_contract}" 28170)
            set_tests_properties(experiment.perf_fixture_${_contract} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        endforeach()
        add_executable(perf_measurement_tests MeasurementTests.cpp)
        target_link_libraries(perf_measurement_tests PRIVATE experiment_perf_fixture)
        add_test(NAME experiment.perf_measurement COMMAND perf_measurement_tests)
        add_executable(perf_resource_tests ResourceTests.cpp)
        target_link_libraries(perf_resource_tests PRIVATE experiment_perf_fixture)
        add_test(NAME experiment.perf_resources COMMAND perf_resource_tests)
        if(WIN32)
            foreach(_target IN ITEMS perf_fixture_tests perf_measurement_tests perf_resource_tests)
                add_custom_command(TARGET ${_target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>" "$<TARGET_FILE_DIR:${_target}>")
            endforeach()
        endif()
    endif()
endif()
