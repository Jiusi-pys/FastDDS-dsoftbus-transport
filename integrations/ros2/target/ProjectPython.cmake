# Only included by project_python.py for its isolated project runtime build.
include("${CMAKE_CURRENT_LIST_DIR}/RosTargetBuildOptions.cmake")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
  message(FATAL_ERROR "Project Python slice requires the OHOS target toolchain")
endif()
foreach(input PROJECT_PYTHON_INCLUDE PROJECT_PYTHON_LIBRARY PROJECT_NUMPY_INCLUDE)
  if(NOT EXISTS "${${input}}")
    message(FATAL_ERROR "Missing explicit target development input: ${input}")
  endif()
endforeach()
# Generated interface exports refer to these targets even when their consumer
# is a C-only package such as rcl_lifecycle. Bind them before find_package calls.
if(NOT TARGET Python3::Python)
  add_library(Python3::Python SHARED IMPORTED GLOBAL)
  set_target_properties(Python3::Python PROPERTIES
    IMPORTED_LOCATION "${PROJECT_PYTHON_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_PYTHON_INCLUDE}")
endif()
if(NOT TARGET Python3::NumPy)
  add_library(Python3::NumPy INTERFACE IMPORTED GLOBAL)
  set_target_properties(Python3::NumPy PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_NUMPY_INCLUDE}")
endif()
set(PYTHON_INSTALL_DIR "lib/python3.12/site-packages" CACHE STRING "Isolated target Python location" FORCE)
set(ROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_INCLUDE_DIR "${PROJECT_PYTHON_INCLUDE}" CACHE PATH "" FORCE)
set(ROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_LIBRARY "${PROJECT_PYTHON_LIBRARY}" CACHE FILEPATH "" FORCE)
set(ROSIDL_GENERATOR_PY_OHOS_TARGET_NUMPY_INCLUDE_DIR "${PROJECT_NUMPY_INCLUDE}" CACHE PATH "" FORCE)
set(ROSIDL_GENERATOR_PY_OHOS_TARGET_EXTENSION_SUFFIX ".cpython-312-aarch64-linux-ohos.so" CACHE STRING "" FORCE)
if(PROJECT_NAME STREQUAL "rmw_implementation")
  # The old project prefix deliberately exports an interface-only target.
  # This slice needs the upstream dispatcher DSO required by observed rclpy.
  set(RMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION OFF CACHE BOOL "" FORCE)
  set(RMW_IMPLEMENTATION rmw_fastrtps_cpp CACHE STRING "" FORCE)
endif()
