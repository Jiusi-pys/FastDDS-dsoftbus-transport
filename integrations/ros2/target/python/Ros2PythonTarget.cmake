# Project include for ROS packages built with Python support for OpenHarmony.
include("${CMAKE_CURRENT_LIST_DIR}/../RosTargetBuildOptions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/FindPython3.cmake") # binds the targets now: generated interface exports name them
set(PYTHON_INSTALL_DIR "lib/python3.12/site-packages" CACHE STRING "Target Python location" FORCE)
# The toolchain confines library searches to its root path, and vendor packages use plain searches to find these.
list(APPEND CMAKE_FIND_ROOT_PATH ${OHOS_DEPENDENCY_ROOTS})
# pluginlib links this target, and the Fast DDS prefix installs tinyxml2 under its older target name.
foreach(_root IN LISTS OHOS_DEPENDENCY_ROOTS)
    if(EXISTS "${_root}/lib/libtinyxml2.so" AND NOT TARGET tinyxml2::tinyxml2)
        add_library(tinyxml2::tinyxml2 SHARED IMPORTED GLOBAL)
        set_target_properties(tinyxml2::tinyxml2 PROPERTIES IMPORTED_LOCATION "${_root}/lib/libtinyxml2.so"
            IMPORTED_SONAME "libtinyxml2.so.6" INTERFACE_INCLUDE_DIRECTORIES "${_root}/include")
    endif()
endforeach()
