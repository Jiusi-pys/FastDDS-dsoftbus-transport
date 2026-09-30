# No SDK or device execution: two Host consumers and an intentional configure rejection.
# Keep earlier compile evidence, including obsolete-option caches. Each test
# invocation owns fresh child build directories rather than overlaying a cache.
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef _variant_run)
foreach(variant host production both)
    set(binary "${OUTPUT_ROOT}/${_variant_run}/${variant}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${ROOT_SOURCE}/tests/cmake/host_facade_variants" -B "${binary}"
        -G "${GENERATOR}" -A x64 "-DFASTDDS_DSOFTBUS_ROOT=${ROOT_SOURCE}" "-DVARIANT=${variant}"
        -DBUILD_TESTING=OFF -DBUILD_INTERFACE_PROBE=OFF -DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF
        -DBUILD_DSOFTBUS_HOST_PROVIDER_LOOP=ON -DBUILD_DSOFTBUS_PROVIDER_ADAPTER_PHASE0=ON
        -DBUILD_DSOFTBUS_TRANSPORT_FACADE=ON -DFASTDDS_DSOFTBUS_FASTDDS_MODE=CONFIG
        -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
        "-Dfastdds_DIR=${FASTDDS_DIR}" "-Dfastcdr_DIR=${FASTCDR_DIR}" "-Dfoonathan_memory_DIR=${FOONATHAN_DIR}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(WRITE "${binary}/configure.log" "${out}\n${err}\nexit=${rc}\n")
    if(variant STREQUAL "both")
        if(rc EQUAL 0 OR NOT "${out}${err}" MATCHES "INTERFACE_FASTDDS_DSOFTBUS_FACADE_VARIANT")
            message(FATAL_ERROR "both variants not rejected for the required interface conflict: ${rc} ${err}")
        endif()
    else()
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${variant} configure failed: ${rc} ${err}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --config Release --parallel 4
            --target facade_variant_consumer RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
        file(WRITE "${binary}/build.log" "${out}\n${err}\nexit=${rc}\n")
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${variant} build failed: ${rc} ${err}")
        endif()
        execute_process(COMMAND "${binary}/Release/facade_variant_consumer.exe"
            RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
        file(WRITE "${binary}/run.log" "${out}\n${err}\nexit=${rc}\n")
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${variant} default constructor changed: ${rc} ${err}")
        endif()
    endif()
endforeach()
message(STATUS "PASS: both defaults unavailable; dual-variant consumer rejected")
