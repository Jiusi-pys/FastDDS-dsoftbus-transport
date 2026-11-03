cmake_minimum_required(VERSION 3.22)
project(Stage1CoordinationControlHost LANGUAGES NONE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_test(NAME stage1.coordination.positive_window
    COMMAND "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_LIST_DIR}/test_positive_window.py")
set_tests_properties(stage1.coordination.positive_window PROPERTIES TIMEOUT 25
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_custom_target(check-positive-window-coordinator
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure -R "^stage1.coordination.positive_window$"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" USES_TERMINAL VERBATIM)
add_custom_target(coordinator-syntax
    COMMAND "${Python3_EXECUTABLE}" -I -B -c
        "import pathlib; [compile(p.read_bytes(), str(p), 'exec') for p in [pathlib.Path(r'${_root}/tools/stage1_coordination/positive_window.py'), pathlib.Path(r'${CMAKE_CURRENT_LIST_DIR}/test_positive_window.py')]]"
    VERBATIM)
