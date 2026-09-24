cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(matrix_contract_tests LANGUAGES CXX)
    include(CTest)
    option(MATRIX_VALIDATOR_ONLY "Build only portable contract tests" ON)
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(matrix_validator_tests validator_tests.cpp)
    target_compile_features(matrix_validator_tests PRIVATE cxx_std_17)
    add_test(NAME experiment.matrix_validator COMMAND matrix_validator_tests)
    if(NOT MATRIX_VALIDATOR_ONLY)
        find_program(MATRIX_NODE_EXECUTABLE NAMES node REQUIRED)
        add_test(NAME experiment.matrix_phase_controller COMMAND "${MATRIX_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/matrix_phase_controller_tests.cjs")
        set_tests_properties(experiment.matrix_phase_controller PROPERTIES TIMEOUT 30)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME experiment.matrix_analyzer COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/dds_matrix_analysis_tests.py")
        set_tests_properties(experiment.matrix_analyzer PROPERTIES TIMEOUT 30)
    endif()
endif()
if(MATRIX_VALIDATOR_ONLY)
    return()
endif()
add_library(experiment_matrix_fixture STATIC PortableFixture.cpp
    "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world/HelloWorldPubSubTypes.cxx"
    "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world/HelloWorldTypeObjectSupport.cxx")
target_compile_features(experiment_matrix_fixture PUBLIC cxx_std_17)
target_include_directories(experiment_matrix_fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FASTDDS_SOURCE_DIR}/examples/cpp/hello_world")
target_link_libraries(experiment_matrix_fixture PUBLIC experimental_dsoftbus_transport fastcdr)
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(matrix_reference reference_main.cpp)
    target_link_libraries(matrix_reference PRIVATE experiment_matrix_fixture)
    if(BUILD_TESTING)
        add_test(NAME experiment.matrix_control_receipts COMMAND "${MATRIX_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/reference_control_tests.cjs" "$<TARGET_FILE:matrix_reference>")
        set_tests_properties(experiment.matrix_control_receipts PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
        # Test selectors only; the sole expected topology/QoS/sample contract is
        # the hash-bound frozen matrix carried by the self-contained trace fixture.
        foreach(_matrix_case IN ITEMS M1 M2 M3 M4 Q_BE_BE Q_REL_BE Q_REL_REL Q_BE_REL L1 L2 L3 I1 I2)
            add_test(NAME experiment.matrix_reference_${_matrix_case}
                COMMAND "${MATRIX_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/MatrixReferenceHarness.cjs"
                --exe "$<TARGET_FILE:matrix_reference>" --case "${_matrix_case}"
                --trace-fixture "${PROJECT_SOURCE_DIR}/tests/matrix_trace_fixtures.json"
                --python "${Python3_EXECUTABLE}"
                --output-root "${CMAKE_BINARY_DIR}/matrix-reference")
            set_tests_properties(experiment.matrix_reference_${_matrix_case}
                PROPERTIES TIMEOUT 225 RUN_SERIAL TRUE)
        endforeach()
    endif()
    if(WIN32)
        add_custom_command(TARGET matrix_reference POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:matrix_reference>")
    endif()
endif()
