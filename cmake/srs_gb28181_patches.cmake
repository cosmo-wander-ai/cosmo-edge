set(SRS_GB28181_PATCH_SCRIPTS
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_managed.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_queue.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_loss.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_audio.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_frames.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/patch_srs_gb28181_rtmp_control.cmake"
)

# Include this file for dependencies; run it with cmake -P to apply the chain.
if(NOT CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    return()
endif()

if(NOT DEFINED SRS_SOURCE_DIR)
    message(FATAL_ERROR "SRS_SOURCE_DIR is required")
endif()

foreach(patch_script IN LISTS SRS_GB28181_PATCH_SCRIPTS)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSRS_SOURCE_DIR=${SRS_SOURCE_DIR}" -P "${patch_script}"
        RESULT_VARIABLE patch_result
    )
    if(NOT patch_result STREQUAL "0")
        message(FATAL_ERROR "SRS GB28181 patch failed: ${patch_script} (${patch_result})")
    endif()
endforeach()
