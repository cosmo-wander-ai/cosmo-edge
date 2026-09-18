##########################################################
# Web Frontend Build (Vue 3 + Vite)
##########################################################
find_program(NPM_EXECUTABLE npm REQUIRED)
find_program(BASH_EXECUTABLE bash REQUIRED)
set(WEB_BUILD_DIR ${CMAKE_BINARY_DIR}/web)
set(WEB_SRC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/src/web)
set(WEB_STAGE_DIR ${WEB_BUILD_DIR}/web_unified)
set(WEB_ENTRY     ${WEB_STAGE_DIR}/dist/index.html)
if(DEFINED RESOURCE_DIR AND NOT "${RESOURCE_DIR}" STREQUAL "")
    set(WEB_RESOURCE_DIR ${RESOURCE_DIR})
else()
    set(WEB_RESOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/resource/aiboxresource_bm1688)
endif()

file(GLOB_RECURSE WEB_SRC_FILES CONFIGURE_DEPENDS
    ${WEB_SRC_DIR}/src/*
    ${WEB_SRC_DIR}/public/*
    ${WEB_SRC_DIR}/scripts/*
)
list(APPEND WEB_SRC_FILES
    ${CMAKE_CURRENT_LIST_FILE}
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/i18n/GLOSSARY.md
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/i18n/SHORT-SCOPES.md
    ${CMAKE_CURRENT_SOURCE_DIR}/data/resource/aiboxresource_bm1688/model_template/yolov8_det.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/resource/aiboxresource_cv186x/model_template/yolov8_det.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/resource/aiboxresource_x86/model_template/yolov8_det.json
    ${WEB_RESOURCE_DIR}/i18n/resource.en-US.json
    ${WEB_RESOURCE_DIR}/i18n/resource.zh-CN.json
    ${WEB_SRC_DIR}/.npmrc
    ${WEB_SRC_DIR}/index.html
    ${WEB_SRC_DIR}/package-lock.json
    ${WEB_SRC_DIR}/package.json
    ${WEB_SRC_DIR}/vite.config.js
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_npm_dependencies.sh
)

file(MAKE_DIRECTORY ${WEB_BUILD_DIR})

add_custom_command(
    OUTPUT  ${WEB_ENTRY}
    DEPENDS ${WEB_SRC_FILES}
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${WEB_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WEB_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEB_SRC_DIR}/src" "${WEB_STAGE_DIR}/src"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEB_SRC_DIR}/public" "${WEB_STAGE_DIR}/public"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${WEB_SRC_DIR}/scripts" "${WEB_STAGE_DIR}/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEB_SRC_DIR}/.npmrc" "${WEB_STAGE_DIR}/.npmrc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEB_SRC_DIR}/index.html" "${WEB_STAGE_DIR}/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEB_SRC_DIR}/package-lock.json" "${WEB_STAGE_DIR}/package-lock.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEB_SRC_DIR}/package.json" "${WEB_STAGE_DIR}/package.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WEB_SRC_DIR}/vite.config.js" "${WEB_STAGE_DIR}/vite.config.js"
    COMMAND ${BASH_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_npm_dependencies.sh"
            "${WEB_STAGE_DIR}"
    # The checked-in public copy may target another platform. Sync only
    # the isolated build copy; sync also performs the strict locale check.
    COMMAND ${CMAKE_COMMAND} -E chdir "${WEB_STAGE_DIR}"
            ${CMAKE_COMMAND} -E env "AIBOX_RESOURCE_DIR=${WEB_RESOURCE_DIR}"
            ${NPM_EXECUTABLE} run resource-i18n:sync
    COMMAND ${CMAKE_COMMAND} -E chdir "${WEB_STAGE_DIR}"
            ${CMAKE_COMMAND} -E env "COSMO_REPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            ${NPM_EXECUTABLE} run build
    COMMENT "Building unified web frontend (Vue 3 + Vite)..."
    VERBATIM
)
add_custom_target(web_frontend ALL DEPENDS ${WEB_ENTRY})
# Serialize the frontend after the engine so compiler and npm/Vite output do
# not interleave in canonical package-build logs.
add_dependencies(web_frontend ${EXECUTABLE_NAME})
