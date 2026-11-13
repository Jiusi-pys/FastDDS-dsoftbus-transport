cmake_minimum_required(VERSION 3.22)
project(StandardsJavaBackend LANGUAGES C CXX)
if(NOT WIN32 OR CMAKE_CROSSCOMPILING OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "B5 runtime target is Windows x64; device Java is outside this milestone")
endif()
find_package(Java 17 REQUIRED COMPONENTS Development Runtime)
find_package(JNI REQUIRED)
find_package(Threads REQUIRED)
if(B4_DSOFTBUS)
    message(FATAL_ERROR "B5 host JNI uses B4 UDP only; device Java is not enabled")
endif()
include(CTest)
add_subdirectory(../c_backend b4)
file(GLOB_RECURSE java_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.java")
file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.java")
set(classes "${CMAKE_CURRENT_BINARY_DIR}/classes")
set(test_classes "${CMAKE_CURRENT_BINARY_DIR}/test-classes")
set(jni_headers "${CMAKE_CURRENT_BINARY_DIR}/jni-headers")
set(api_jar "${CMAKE_CURRENT_BINARY_DIR}/standards-b5.jar")
set(test_jar "${CMAKE_CURRENT_BINARY_DIR}/standards-b5-tests.jar")
add_custom_command(OUTPUT "${api_jar}" "${jni_headers}/org_fastdds_standards_b5_NativeBridge.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${classes}" "${jni_headers}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -encoding UTF-8 -Xlint:all -Werror -h "${jni_headers}" -d "${classes}" ${java_sources}
    COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${api_jar}" -C "${classes}" .
    DEPENDS ${java_sources} VERBATIM)
add_custom_target(b5_java_api ALL DEPENDS "${api_jar}" "${jni_headers}/org_fastdds_standards_b5_NativeBridge.h")
add_library(standards_b5_jni SHARED jni.cpp)
add_dependencies(standards_b5_jni b5_java_api)
target_compile_features(standards_b5_jni PRIVATE cxx_std_17)
target_include_directories(standards_b5_jni PRIVATE "${jni_headers}" ${JNI_INCLUDE_DIRS})
target_compile_definitions(standards_b5_jni PRIVATE B5_ABI="${DDS_STANDARDS_FASTDDS}")
target_link_libraries(standards_b5_jni PRIVATE standards_b4)
add_custom_command(OUTPUT "${test_jar}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${test_classes}"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -encoding UTF-8 -Xlint:all -Werror -cp "${classes}" -d "${test_classes}" ${test_sources}
    COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${test_jar}" -C "${classes}" . -C "${test_classes}" .
    DEPENDS ${test_sources} "${api_jar}" VERBATIM)
add_custom_target(b5_java_tests ALL DEPENDS "${test_jar}")
add_dependencies(b5_java_tests b5_java_api)
set(B5_TEST_DOMAIN 219 CACHE STRING "Host acceptance domain")
foreach(mode record native concurrency wrong-abi)
    add_test(NAME standards.b5_${mode} COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni -cp "${test_jar}"
        org.fastdds.standards.b5.Contract "${mode}" "$<TARGET_FILE:standards_b5_jni>" "${DDS_STANDARDS_FASTDDS}" "${B5_TEST_DOMAIN}")
endforeach()
add_library(standards_b5_fault_jni SHARED jni.cpp tests/fake_b4.cpp)
add_dependencies(standards_b5_fault_jni b5_java_api)
target_compile_features(standards_b5_fault_jni PRIVATE cxx_std_17)
target_include_directories(standards_b5_fault_jni PRIVATE "${jni_headers}" ${JNI_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/../c_backend")
target_compile_definitions(standards_b5_fault_jni PRIVATE B5_ABI="${DDS_STANDARDS_FASTDDS}" B5_FAKE_BACKEND=1)
target_link_libraries(standards_b5_fault_jni PRIVATE Threads::Threads)
add_test(NAME standards.b5_synthetic_ownership COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni -cp "${test_jar}"
    org.fastdds.standards.b5.FaultContract "$<TARGET_FILE:standards_b5_fault_jni>" "${DDS_STANDARDS_FASTDDS}")
add_test(NAME standards.b5_wrong_backend COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni -cp "${test_jar}"
    org.fastdds.standards.b5.Contract wrong-backend "$<TARGET_FILE:standards_b5_fault_jni>" "${DDS_STANDARDS_FASTDDS}" "${B5_TEST_DOMAIN}")
