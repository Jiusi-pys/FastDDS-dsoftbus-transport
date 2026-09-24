add_executable(matrix_native_config_tests matrix_native_config_tests.cpp)
target_link_libraries(matrix_native_config_tests PRIVATE experimental_dsoftbus_transport)
add_test(NAME experiment.matrix_native_config COMMAND matrix_native_config_tests)
set_tests_properties(experiment.matrix_native_config PROPERTIES TIMEOUT 15)
if(WIN32)
    add_custom_command(TARGET matrix_native_config_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
        "$<TARGET_FILE_DIR:matrix_native_config_tests>")
endif()
