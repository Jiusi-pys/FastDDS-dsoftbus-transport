cmake_minimum_required(VERSION 3.22)
project(ROS214CandidateParserRegressions LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
include(CTest)
include(GoogleTest)
find_package(fastcdr 2.2.8 EXACT CONFIG REQUIRED)
find_package(foonathan_memory CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
if(NOT IS_ABSOLUTE "${CANDIDATE_SOURCE}" OR NOT IS_ABSOLUTE "${CANDIDATE_BUILD}")
    message(FATAL_ERROR "Explicit prepared candidate and its configured build required")
endif()
file(READ "${CANDIDATE_SOURCE}/FDDST_MAINTENANCE_PREPARED.json" _receipt)
if(NOT _receipt MATCHES "010b908dc3bb2b6f2818d61ac8db462d01b79a1f")
    message(FATAL_ERROR "Parser tests require pinned prepared maintenance source")
endif()
# Include upstream's own target definition unchanged, with its project-relative
# inputs pointing to the exact candidate. It compiles real serializers/parsers.
set(PROJECT_SOURCE_DIR "${CANDIDATE_SOURCE}")
set(PROJECT_BINARY_DIR "${CANDIDATE_BUILD}")
add_compile_definitions(FASTRTPS_NO_LIB)
include_directories("${CANDIDATE_SOURCE}/thirdparty/filewatch"
    "${CANDIDATE_SOURCE}/thirdparty/nlohmann-json"
    "${CANDIDATE_SOURCE}/thirdparty/boost/include"
    "${CANDIDATE_SOURCE}/thirdparty/taocpp-pegtl")
set(LINK_SSL OFF)
add_subdirectory("${CANDIDATE_SOURCE}/test/unittest/rtps/builtin" upstream-builtin)
add_subdirectory("${CANDIDATE_SOURCE}/test/unittest/dds/topic/DDSSQLFilter" upstream-sql)
