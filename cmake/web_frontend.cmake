##########################################################
# Web Frontend Build (Vue 3 + Vite)
##########################################################
find_program(NPM_EXECUTABLE npm REQUIRED)
set(WEB_BUILD_DIR ${CMAKE_BINARY_DIR}/web)
set(WEB_SRC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/src/web)
set(WEB_STAMP     ${WEB_BUILD_DIR}/web_unified.stamp)

file(GLOB_RECURSE WEB_SRC_FILES
    ${WEB_SRC_DIR}/src/*
    ${WEB_SRC_DIR}/index.html
)
list(APPEND WEB_SRC_FILES
    ${WEB_SRC_DIR}/package.json
    ${WEB_SRC_DIR}/vite.config.js
)

file(MAKE_DIRECTORY ${WEB_BUILD_DIR}/web_unified)

add_custom_command(
    OUTPUT  ${WEB_STAMP}
    DEPENDS ${WEB_SRC_FILES}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_SRC_DIR} ${WEB_BUILD_DIR}/web_unified
    COMMAND ${CMAKE_COMMAND} -E rm -f ${WEB_BUILD_DIR}/web_unified/package-lock.json
    COMMAND ${NPM_EXECUTABLE} install --loglevel=error
    COMMAND ${NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E touch ${WEB_STAMP}
    WORKING_DIRECTORY ${WEB_BUILD_DIR}/web_unified
    COMMENT "Building unified web frontend (Vue 3 + Vite)..."
)
add_custom_target(web_frontend ALL DEPENDS ${WEB_STAMP})
add_dependencies(web_frontend ${EXECUTABLE_NAME})
