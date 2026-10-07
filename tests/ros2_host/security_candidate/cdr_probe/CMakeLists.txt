cmake_minimum_required(VERSION 3.22)
project(Cve63340MalformedCdrProbe LANGUAGES CXX)

# Malformed CDR sequence-length tests for the current application data type.
#
# This project deliberately uses the candidate's default SECURITY=OFF prefix,
# because the generated std_msgs typesupport it links was rebuilt against that
# prefix in the same candidate. Malformed-sequence handling is independent of
# DDS Security; the security probes are the targets that use SECURITY=ON.
#
# Required, all absolute:
#   CDR_PROBE_STD_MSGS_DIR       build dir of the generated std_msgs package
#   CDR_PROBE_TYPESUPPORT_DIR    build dir of the rebuilt rosidl_typesupport_fastrtps_cpp
#   CDR_PROBE_ROS_INSTALL        ROS 2 Windows installation providing rosidl_runtime_c/cpp

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fastrtps 2.14.7 EXACT CONFIG REQUIRED)
find_package(fastcdr 2.2.8 EXACT CONFIG REQUIRED)
find_package(foonathan_memory CONFIG REQUIRED)

foreach(_required CDR_PROBE_STD_MSGS_DIR CDR_PROBE_TYPESUPPORT_DIR CDR_PROBE_ROS_INSTALL)
    if(NOT IS_ABSOLUTE "${${_required}}")
        message(FATAL_ERROR "${_required} must be an absolute path")
    endif()
endforeach()

add_executable(malformed_cdr_sequence_tests malformed_cdr_sequence_tests.cpp)

# ROS 2 on Windows installs each package under include/<package>/<package>/...,
# so every package root is its own include directory.
foreach(_ros_package rosidl_runtime_c rosidl_runtime_cpp rosidl_typesupport_cpp
        rosidl_typesupport_fastrtps_cpp rosidl_typesupport_interface rcutils)
    if(IS_DIRECTORY "${CDR_PROBE_ROS_INSTALL}/include/${_ros_package}")
        list(APPEND _ros_include_dirs "${CDR_PROBE_ROS_INSTALL}/include/${_ros_package}")
    endif()
endforeach()

target_include_directories(malformed_cdr_sequence_tests PRIVATE
    "${CDR_PROBE_STD_MSGS_DIR}/rosidl_typesupport_fastrtps_cpp"
    "${CDR_PROBE_STD_MSGS_DIR}/rosidl_generator_c"
    "${CDR_PROBE_STD_MSGS_DIR}/rosidl_generator_cpp"
    "${CDR_PROBE_ROS_INSTALL}/include"
    ${_ros_include_dirs})

target_link_directories(malformed_cdr_sequence_tests PRIVATE
    "${CDR_PROBE_ROS_INSTALL}/lib")

target_link_libraries(malformed_cdr_sequence_tests PRIVATE
    "${CDR_PROBE_TYPESUPPORT_DIR}/Release/rosidl_typesupport_fastrtps_cpp.lib"
    "${CDR_PROBE_STD_MSGS_DIR}/Release/std_msgs__rosidl_typesupport_fastrtps_cpp.lib"
    "${CDR_PROBE_STD_MSGS_DIR}/Release/std_msgs__rosidl_generator_c.lib"
    "${CDR_PROBE_ROS_INSTALL}/lib/rosidl_runtime_c.lib"
    "${CDR_PROBE_ROS_INSTALL}/lib/rcutils.lib"
    fastcdr fastrtps foonathan_memory)

if(WIN32)
    foreach(_dep fastrtps fastcdr)
        if(TARGET ${_dep})
            add_custom_command(TARGET malformed_cdr_sequence_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_dep}>
                    $<TARGET_FILE_DIR:malformed_cdr_sequence_tests>)
        endif()
    endforeach()
endif()
