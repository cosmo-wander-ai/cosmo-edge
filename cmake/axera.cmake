# AXERA's board-specific SDK must match the deployed firmware. The runtime
# remains external to the source tree and is not silently replaced on-device.
set(COSMO_AXERA_ROOT "" CACHE PATH "AXERA SDK root containing include/ and lib/")
if(NOT COSMO_AXERA_ROOT AND DEFINED ENV{AXERA_ROOT})
    set(COSMO_AXERA_ROOT "$ENV{AXERA_ROOT}" CACHE PATH "AXERA SDK root" FORCE)
endif()
if(NOT COSMO_AXERA_ROOT)
    message(FATAL_ERROR "COSMO_AXERA_ROOT is required for the AXERA backend")
endif()

# Cross-linkers also need the target SDK's directory when resolving transitive
# DT_NEEDED dependencies (for example VDEC's libturbojpeg). This is a build
# search path only; it does not copy or replace runtime libraries on the board.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options("-Wl,-rpath-link,${COSMO_AXERA_ROOT}/lib")
endif()

set(_axera_include "${COSMO_AXERA_ROOT}/include")
foreach(_header ax_sys_api.h ax_engine_api.h)
    if(NOT EXISTS "${_axera_include}/${_header}")
        message(FATAL_ERROR "AXERA SDK header not found: ${_axera_include}/${_header}")
    endif()
endforeach()

set(_axera_components sys engine)
if(COSMO_MEDIA_USE_AXERA_BACKEND)
    if(NOT EXISTS "${_axera_include}/ax_vdec_api.h")
        message(FATAL_ERROR "AXERA VDEC header not found: ${_axera_include}/ax_vdec_api.h")
    endif()
    list(APPEND _axera_components vdec ivps)
endif()
foreach(_component IN LISTS _axera_components)
    set(_library "${COSMO_AXERA_ROOT}/lib/libax_${_component}.so")
    if(NOT EXISTS "${_library}")
        message(FATAL_ERROR "AXERA SDK library not found: ${_library}")
    endif()
    add_library(axera_${_component} SHARED IMPORTED GLOBAL)
    set_target_properties(axera_${_component} PROPERTIES
        IMPORTED_LOCATION "${_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_axera_include}")
endforeach()
