cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(mdds_keyed_type_contract LANGUAGES CXX)
    include(CTest)
    set(MDDS_FASTDDS_MODE CONFIG)
    get_filename_component(_keyed_repository "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
    include("${_keyed_repository}/cmake/FastDDSBinding.cmake")
endif()

add_library(experiment_keyed_type STATIC
    MddsKeyedSamplePubSubTypes.cxx MddsKeyedSampleTypeObjectSupport.cxx)
target_compile_features(experiment_keyed_type PUBLIC cxx_std_17)
target_include_directories(experiment_keyed_type PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(experiment_keyed_type PUBLIC mdds_fastdds_binding fastcdr)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(keyed_type_tests type_tests.cpp)
    target_link_libraries(keyed_type_tests PRIVATE experiment_keyed_type)
    foreach(_contract IN ITEMS key non_key md5 roundtrip type_object with_key)
        add_test(NAME experiment.keyed_type_${_contract} COMMAND keyed_type_tests "${_contract}")
        set_tests_properties(experiment.keyed_type_${_contract} PROPERTIES TIMEOUT 30)
    endforeach()
    if(WIN32)
        add_custom_command(TARGET keyed_type_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:fastdds>" "$<TARGET_FILE:fastcdr>" "$<TARGET_FILE:foonathan_memory>"
                "$<TARGET_FILE_DIR:keyed_type_tests>")
    endif()
endif()
