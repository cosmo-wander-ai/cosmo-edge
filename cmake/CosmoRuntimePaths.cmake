set(_COSMO_RUNTIME_PATHS_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(cosmo_configure_runtime_paths target_chip output_dir)
    string(TOLOWER "${target_chip}" normalized_target_chip)

    set(COSMO_DEFAULT_DATA_DIR "/data/cwaiuserdata")
    if(normalized_target_chip MATCHES "^(rk3576|rk3588|rv1126b)$")
        set(COSMO_DEFAULT_DATA_DIR "/userdata/cwaiuserdata")
    endif()
    set(COSMO_DEFAULT_APP_DATA_DIR "/appfs/cosmo_wander/cwai_data")

    file(MAKE_DIRECTORY "${output_dir}")
    configure_file(
        "${_COSMO_RUNTIME_PATHS_MODULE_DIR}/runtime-paths.env.in"
        "${output_dir}/runtime-paths.env"
        @ONLY
        NEWLINE_STYLE UNIX)
    configure_file(
        "${_COSMO_RUNTIME_PATHS_MODULE_DIR}/RuntimePathsConfig.h.in"
        "${output_dir}/RuntimePathsConfig.h"
        @ONLY
        NEWLINE_STYLE UNIX)

    set(COSMO_DEFAULT_DATA_DIR "${COSMO_DEFAULT_DATA_DIR}" PARENT_SCOPE)
    set(COSMO_DEFAULT_APP_DATA_DIR "${COSMO_DEFAULT_APP_DATA_DIR}" PARENT_SCOPE)
    set(COSMO_RUNTIME_PATHS_ENV "${output_dir}/runtime-paths.env" PARENT_SCOPE)
    set(COSMO_RUNTIME_PATHS_INCLUDE_DIR "${output_dir}" PARENT_SCOPE)
endfunction()
