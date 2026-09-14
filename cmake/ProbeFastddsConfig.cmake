# Generate the upstream config template for THIS standalone probe only.
# No conclusions about a full Fast DDS configuration or optional subsystems follow.
file(STRINGS "${FASTDDS_SOURCE_DIR}/CMakeLists.txt" fastdds_project_line
    REGEX "^project\\(fastdds VERSION")
if(NOT fastdds_project_line MATCHES "VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
    message(FATAL_ERROR "Cannot extract the Fast DDS version from the selected source checkout")
endif()
set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
set(PROJECT_VERSION_TWEAK "${CMAKE_MATCH_4}")
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")
message(STATUS "Fast DDS source-interface probe: ${PROJECT_VERSION}, ${FASTDDS_SOURCE_DIR}")
set(HAVE_CXX20 0)
set(HAVE_CXX17 1)
set(HAVE_CXX14 1)
set(HAVE_CXX1Y 1)
set(HAVE_CXX11 1)
include(TestBigEndian)
test_big_endian(FASTDDS_IS_BIG_ENDIAN_TARGET)
foreach(feature IN ITEMS HAVE_SECURITY HAVE_SQLITE3 TLS_FOUND HAVE_STRICT_REALTIME
        ENABLE_OLD_LOG_MACROS_ HAVE_LOG_NO_INFO HAVE_LOG_NO_WARNING HAVE_LOG_NO_ERROR)
    set(${feature} 0)
endforeach()
set(USE_THIRDPARTY_SHARED_MUTEX OFF)
configure_file("${FASTDDS_SOURCE_DIR}/include/fastdds/config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/fastdds/config.hpp" @ONLY)
