cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED TEST_PROJECT_ROOT)
    message(FATAL_ERROR "TEST_PROJECT_ROOT is required")
endif()

if(NOT DEFINED TEST_OUTPUT_DIR)
    message(FATAL_ERROR "TEST_OUTPUT_DIR is required")
endif()

set(upstream "3rd/srs-6.0-r0/trunk")
set(patch_manifest "${TEST_PROJECT_ROOT}/cmake/srs_gb28181_patches.cmake")
set(test_source_dir "${TEST_OUTPUT_DIR}/srs-source")
set(test_source_file "${test_source_dir}/src/app/srs_app_gb28181.cpp")
set(patched_files
    src/app/srs_app_gb28181.cpp
    src/app/srs_app_gb28181.hpp
    src/protocol/srs_protocol_rtmp_conn.cpp
    src/protocol/srs_protocol_rtmp_conn.hpp
    src/protocol/srs_protocol_rtmp_stack.cpp
    src/protocol/srs_protocol_rtmp_stack.hpp
)

function(apply_chain)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSRS_SOURCE_DIR=${test_source_dir}" -P "${patch_manifest}"
        RESULT_VARIABLE result
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "SRS GB28181 patch chain failed: ${result}")
    endif()
endfunction()

function(hash_patched_files source_dir output)
    set(hashes "")
    foreach(name IN LISTS patched_files)
        file(SHA256 "${source_dir}/${name}" hash)
        list(APPEND hashes "${hash}")
    endforeach()
    set(${output} "${hashes}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${TEST_OUTPUT_DIR}")
foreach(name IN LISTS patched_files)
    configure_file("${TEST_PROJECT_ROOT}/${upstream}/${name}"
        "${test_source_dir}/${name}" COPYONLY)
endforeach()
apply_chain()
hash_patched_files("${test_source_dir}" fresh_hashes)

file(READ "${test_source_file}" fresh_source)
string(REGEX MATCHALL
    "media\\.session_info_\\.setup_ = \"passive\""
    setup_matches
    "${fresh_source}")
list(LENGTH setup_matches setup_match_count)
if(NOT setup_match_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one passive TCP setup attribute, found ${setup_match_count}")
endif()

string(FIND "${fresh_source}"
    "media.protos_ = \"TCP/RTP/AVP\";\n    media.session_info_.setup_ = \"passive\";"
    setup_position)
if(setup_position EQUAL -1)
    message(FATAL_ERROR
        "Passive TCP setup attribute was not added to the GB28181 video offer")
endif()

foreach(required "COSMO_MANAGED_GB_V1" "local POST required" "external media lease expired"
        "if (receiver_) receiver_->interrupt()" "if (sender_) sender_->interrupt()"
        "external SIP required" "stop_external()" "invalid SSRC")
    string(FIND "${fresh_source}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Managed GB28181 safety guard missing: ${required}")
    endif()
endforeach()

foreach(name IN LISTS patched_files)
    file(READ "${test_source_dir}/${name}" control_part)
    if(NOT control_part MATCHES "COSMO_GB_RTMP_CONTROL_V2" OR
            control_part MATCHES "COSMO_GB_RTMP_CONTROL_V1")
        message(FATAL_ERROR "GB RTMP control V2 patch missing/incomplete: ${name}")
    endif()
endforeach()

apply_chain()
hash_patched_files("${test_source_dir}" reapplied_hashes)
if(NOT reapplied_hashes STREQUAL fresh_hashes)
    message(FATAL_ERROR "SRS GB28181 patch chain is not idempotent")
endif()

# Simulate an existing build copy from PR #257 before the trailing-NAL fix.
string(REPLACE "COSMO_GB_ACCESS_UNIT_V2" "COSMO_GB_ACCESS_UNIT_V1"
    legacy_frames_source "${fresh_source}")
string(REPLACE [=[            srs_warn("GB: Ignore NALU nt=%d, frame=[%s]", nt, bytes.c_str());
            break;]=] [=[            srs_warn("GB: Ignore NALU nt=%d, frame=[%s]", nt, bytes.c_str());
            return err;]=] legacy_frames_source "${legacy_frames_source}")
if(legacy_frames_source STREQUAL fresh_source)
    message(FATAL_ERROR "Failed to prepare the V1 access-unit patch upgrade fixture")
endif()
file(WRITE "${test_source_file}" "${legacy_frames_source}")
apply_chain()
hash_patched_files("${test_source_dir}" upgraded_hashes)
if(NOT upgraded_hashes STREQUAL fresh_hashes)
    message(FATAL_ERROR "Failed to upgrade the V1 access-unit patch")
endif()

# Use the real ExternalProject patch step without configuring or compiling SRS.
include("${patch_manifest}")
set(fixture_project "${TEST_OUTPUT_DIR}/incremental-project")
set(fixture_build "${TEST_OUTPUT_DIR}/incremental-build")
file(COPY "${TEST_PROJECT_ROOT}/cmake/srs.cmake" "${patch_manifest}"
    ${SRS_GB28181_PATCH_SCRIPTS} DESTINATION "${fixture_project}/cmake")
foreach(name IN LISTS patched_files)
    configure_file("${TEST_PROJECT_ROOT}/${upstream}/${name}"
        "${fixture_project}/${upstream}/${name}" COPYONLY)
endforeach()
file(WRITE "${fixture_project}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.16)
project(SrsPatchStep NONE)
if(POLICY CMP0114)
    cmake_policy(SET CMP0114 NEW)
endif()
include(ExternalProject)
set(THIRDPARTY_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/thirdparty")
add_custom_target(third_build)
include(cmake/srs.cmake)
ExternalProject_Add_StepTargets(srs_external patch)
]=])
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${fixture_project}" -B "${fixture_build}"
    RESULT_VARIABLE result
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Failed to configure SRS ExternalProject fixture: ${result}")
endif()

function(build_patch_step expect_patch)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${fixture_build}" --target srs_external-patch
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "SRS ExternalProject patch step failed: ${output}${error}")
    endif()
    if((expect_patch AND NOT output MATCHES "Performing patch step") OR
            (NOT expect_patch AND output MATCHES "Performing patch step"))
        message(FATAL_ERROR "Unexpected SRS ExternalProject patch execution: ${output}")
    endif()
endfunction()

build_patch_step(TRUE)
hash_patched_files("${fixture_build}/srs_source" build_hashes)
if(NOT build_hashes STREQUAL fresh_hashes)
    message(FATAL_ERROR "ExternalProject patch output differs from the fresh chain")
endif()
build_patch_step(FALSE)

# Queue was previously missing from the step's file dependencies.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
set(queue_patch "${fixture_project}/cmake/patch_srs_gb28181_queue.cmake")
file(READ "${queue_patch}" queue_patch_content)
file(WRITE "${queue_patch}" [=[file(WRITE "${SRS_SOURCE_DIR}/queue-patch-ran" "")
]=] "${queue_patch_content}")
build_patch_step(TRUE)
if(NOT EXISTS "${fixture_build}/srs_source/queue-patch-ran")
    message(FATAL_ERROR "The changed queue patch did not run in the existing build")
endif()
