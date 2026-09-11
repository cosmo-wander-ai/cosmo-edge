cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_PROJECT_ROOT)
    message(FATAL_ERROR "TEST_PROJECT_ROOT is required")
endif()

if(NOT DEFINED TEST_OUTPUT_DIR)
    message(FATAL_ERROR "TEST_OUTPUT_DIR is required")
endif()

set(source_file
    "${TEST_PROJECT_ROOT}/3rd/srs-6.0-r0/trunk/src/app/srs_app_gb28181.cpp")
set(patch_script "${TEST_PROJECT_ROOT}/cmake/patch_srs_gb28181.cmake")
set(test_source_dir "${TEST_OUTPUT_DIR}/srs-source")
set(test_source_file "${test_source_dir}/src/app/srs_app_gb28181.cpp")

file(REMOVE_RECURSE "${TEST_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${test_source_dir}/src/app")
configure_file("${source_file}" "${test_source_file}" COPYONLY)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSRS_SOURCE_DIR=${test_source_dir}"
        -P "${patch_script}"
    RESULT_VARIABLE first_patch_result
    OUTPUT_VARIABLE first_patch_output
    ERROR_VARIABLE first_patch_error
)
if(NOT first_patch_result EQUAL 0)
    message(FATAL_ERROR
        "First SRS GB28181 patch failed: ${first_patch_output}${first_patch_error}")
endif()

file(READ "${test_source_file}" first_patched_source)
string(REGEX MATCHALL
    "media\\.session_info_\\.setup_ = \"passive\""
    setup_matches
    "${first_patched_source}")
list(LENGTH setup_matches setup_match_count)
if(NOT setup_match_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one passive TCP setup attribute, found ${setup_match_count}")
endif()

string(FIND "${first_patched_source}"
    "media.protos_ = \"TCP/RTP/AVP\";\n    media.session_info_.setup_ = \"passive\";"
    setup_position)
if(setup_position EQUAL -1)
    message(FATAL_ERROR
        "Passive TCP setup attribute was not added to the GB28181 video offer")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSRS_SOURCE_DIR=${test_source_dir}"
        -P "${patch_script}"
    RESULT_VARIABLE second_patch_result
    OUTPUT_VARIABLE second_patch_output
    ERROR_VARIABLE second_patch_error
)
if(NOT second_patch_result EQUAL 0)
    message(FATAL_ERROR
        "Second SRS GB28181 patch failed: ${second_patch_output}${second_patch_error}")
endif()

file(READ "${test_source_file}" second_patched_source)
if(NOT first_patched_source STREQUAL second_patched_source)
    message(FATAL_ERROR "SRS GB28181 patch is not idempotent")
endif()

# External SIP has separate lifecycle and local-control requirements.
configure_file("${TEST_PROJECT_ROOT}/3rd/srs-6.0-r0/trunk/src/app/srs_app_gb28181.hpp"
    "${test_source_dir}/src/app/srs_app_gb28181.hpp" COPYONLY)
foreach(attempt RANGE 1 2)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSRS_SOURCE_DIR=${test_source_dir}"
        -P "${TEST_PROJECT_ROOT}/cmake/patch_srs_gb28181_managed.cmake"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Managed GB28181 patch failed")
    endif()
    file(READ "${test_source_file}" managed_source)
    if(attempt EQUAL 1)
        set(first_managed_source "${managed_source}")
    elseif(NOT managed_source STREQUAL first_managed_source)
        message(FATAL_ERROR "Managed GB28181 patch is not idempotent")
    endif()
endforeach()

foreach(attempt RANGE 1 2)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSRS_SOURCE_DIR=${test_source_dir}"
        -P "${TEST_PROJECT_ROOT}/cmake/patch_srs_gb28181_queue.cmake"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "GB queue patch failed")
    endif()
    file(READ "${test_source_file}" queue_source)
    if(attempt EQUAL 1)
        set(first_queue_source "${queue_source}")
    elseif(NOT queue_source STREQUAL first_queue_source)
        message(FATAL_ERROR "GB queue patch is not idempotent")
    endif()
endforeach()
foreach(required "COSMO_MANAGED_GB_V1" "local POST required" "external media lease expired"
        "if (receiver_) receiver_->interrupt()" "if (sender_) sender_->interrupt()"
        "external SIP required" "stop_external()" "invalid SSRC")
    string(FIND "${managed_source}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Managed GB28181 safety guard missing: ${required}")
    endif()
endforeach()
