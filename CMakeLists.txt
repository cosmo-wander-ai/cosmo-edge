cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
endif()
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

if(" ${CMAKE_SOURCE_DIR}" STREQUAL " ${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "FATAL: In-source builds are not allowed. You should create a separate directory for build files.")
endif()

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

set(COSMO_TARGET_ARCH "aarch64" CACHE STRING "Target architecture: aarch64 or x86_64")
set_property(CACHE COSMO_TARGET_ARCH PROPERTY STRINGS aarch64 x86_64)
if(NOT COSMO_TARGET_ARCH STREQUAL "aarch64" AND NOT COSMO_TARGET_ARCH STREQUAL "x86_64")
    message(FATAL_ERROR "Unsupported COSMO_TARGET_ARCH=${COSMO_TARGET_ARCH}. Expected aarch64 or x86_64.")
endif()

if(COSMO_TARGET_ARCH STREQUAL "aarch64" AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchains/aarch64-linux.toolchain.cmake"
        CACHE FILEPATH "aarch64 linux cross toolchain")
endif()

project(Cosmo LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Generate compile_commands.json for static analysis tools(clang - tidy, IDE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#Baseline : suppress noisy warnings from third - party headers(global scope)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wno-deprecated-declarations -Wno-ignored-attributes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-ignored-attributes")

#Strict warnings applied only to project OBJECT libraries(not third - party)
set(COSMO_STRICT_WARNINGS
    -Wall
    -Wextra
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wpedantic
    # -Wconversion
    # -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
)

option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    message(STATUS "Enabling code coverage for tests")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

option(COSMO_DEV_MODE "Development mode: disable watchdog and other production-only features" OFF)
if(COSMO_DEV_MODE)
    message(STATUS "Development mode enabled (watchdog disabled)")
    add_compile_definitions(COSMO_DEV_MODE)
endif()

#-- Backend selection(mutually exclusive) --
option(COSMO_NN_USE_SOPHON_BACKEND "Enable Sophon TPU backend" ON)
option(COSMO_NN_USE_CPU_BACKEND "Enable CPU inference backend (ONNX Runtime)" OFF)

if(COSMO_NN_USE_SOPHON_BACKEND AND COSMO_NN_USE_CPU_BACKEND)
    message(FATAL_ERROR "COSMO_NN_USE_SOPHON_BACKEND and COSMO_NN_USE_CPU_BACKEND are mutually exclusive")
endif()
if(NOT COSMO_NN_USE_SOPHON_BACKEND AND NOT COSMO_NN_USE_CPU_BACKEND)
    message(FATAL_ERROR "At least one NN backend must be enabled")
endif()

if(COSMO_NN_USE_CPU_BACKEND)
    set(COSMO_ENABLE_OPENH264 ON)
    set(COSMO_OPENH264_USE_ASM OFF)
    set(COSMO_MODEL_GUARD OFF)
else()
    set(COSMO_ENABLE_OPENH264 OFF)
    set(COSMO_OPENH264_USE_ASM OFF)
    set(COSMO_MODEL_GUARD ON)
endif()

if(COSMO_MODEL_GUARD)
    # .so is pre-installed in system image at /usr/lib/ or provided in prebuild/
    find_library(MODEL_GUARD_LIB cosmo_model_guard
                 PATHS /usr/lib ${CMAKE_SOURCE_DIR}/prebuild
                 NO_DEFAULT_PATH)
    if(MODEL_GUARD_LIB)
        message(STATUS "Model guard enabled: ${MODEL_GUARD_LIB}")
    else()
        message(WARNING "COSMO_MODEL_GUARD=ON but libcosmo_model_guard.so not found — "
                        "encrypted models will fail at runtime")
    endif()
endif()

message(STATUS "=======================")
message(STATUS "Cosmo Build Info:")
message(STATUS "\tSystem:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "\tProcessor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "\tCompiler:  ${CMAKE_CXX_COMPILER}, Version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "\tToolchain: ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "\tTarget:    ${COSMO_TARGET_ARCH}")

##########################################################
#Third - party Dependencies
##########################################################
set(THIRDPARTY_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/thirdparty_install CACHE PATH "thirdparty install dir")
add_custom_target(third_build)

# Pre-create include directories for all ExternalProject-managed libraries.
# CMake validates INTERFACE_INCLUDE_DIRECTORIES paths at configure time, but
# ExternalProject only installs headers at build time.  Without this, a clean
# build (rm -rf build && ./build.sh) fails because the paths don't exist yet.
# Note: ffmpeg and openh264 are excluded — they are now provided as prebuilt
# binaries under prebuild/ffmpeg/<arch>/ and do not use ExternalProject.
foreach(_tp_subdir fmt glog zlib sqlitecpp uuid uSockets mp4v2 openssl curl mqtt event cryptopp tokenizers)
    file(MAKE_DIRECTORY "${THIRDPARTY_INSTALL_PREFIX}/${_tp_subdir}/include")
endforeach()

include(ExternalProject)

include(cmake/fmt.cmake)
include(cmake/glog.cmake)
include(cmake/zlib.cmake)
include(cmake/sqlitecpp.cmake)
include(cmake/uuid.cmake)
include(cmake/uSockets.cmake)
include(cmake/mp4v2.cmake)
include(cmake/openssl.cmake)
include(cmake/curl.cmake)
include(cmake/mqtt.cmake)
include(cmake/event.cmake)
include(cmake/ffmpeg.cmake)
include(cmake/cryptopp.cmake)
include(cmake/uWebSockets.cmake)
include(cmake/tokenizers.cmake)
if(COSMO_NN_USE_SOPHON_BACKEND)
    include(cmake/device.cmake)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    include(cmake/onnxruntime.cmake)
endif()
include(cmake/pcap.cmake)
include(cmake/srs.cmake)

#Header - only third - party libraries
add_library(nlohmann INTERFACE)
target_include_directories(nlohmann SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/include)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/include)

add_library(eigen3 INTERFACE)
target_include_directories(eigen3 SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rd/eigen3-3.2.92)

# Minimal common dependencies shared by nearly all OBJECT libraries.
# Provides SYSTEM include directories for fmt, glog and nlohmann-json,
# which suppresses compiler warnings from these ubiquitous third-party headers.
# Each module declares its own additional third-party deps explicitly.
add_library(cosmo_common INTERFACE)
target_link_libraries(cosmo_common INTERFACE fmt glog nlohmann)

##########################################################
#OBJECT Libraries(per - module incremental compilation)
##########################################################

#-- - Layer 0 : Foundation -- -
add_subdirectory(src/util)

add_subdirectory(src/db)

add_subdirectory(src/platform)

add_subdirectory(src/mem)

add_subdirectory(src/nn)

#-- - Layer 1 : Infrastructure -- -
add_subdirectory(src/media)

add_subdirectory(src/infer)

add_subdirectory(src/network)

#-- - Layer 2 : Business Logic -- -
add_subdirectory(src/linkage)

add_subdirectory(src/service)

add_subdirectory(src/flow)

#-- - Layer 3 : Interface -- -
add_subdirectory(src/api)

#-- - App Entry -- -
file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS "src/app/*.h" "src/app/*.cc")

#All OBJECT library targets(grouped by dependency layer)
#Note : cosmo_nn is excluded — it uses its own compile options(see src/nn/CMakeLists.txt)
set(ALL_OBJECT_LIBS
#Layer 0 — Foundation(no upward dependencies)
    cosmo_util cosmo_db cosmo_platform cosmo_mem
#Layer 1 — Infrastructure
    cosmo_media cosmo_infer cosmo_network
#Layer 2 — Business(linkage depends on service - layer DTO types)
    cosmo_linkage cosmo_service cosmo_flow
#Layer 3 — Interface
    cosmo_api
)

#Apply strict compiler warnings and third-party build ordering to all project OBJECT libraries.
#OBJECT libraries do NOT transitively inherit build-order from IMPORTED targets,
#so we must explicitly add_dependencies to guarantee ExternalProject headers are installed first.
foreach(lib ${ALL_OBJECT_LIBS})
    target_compile_options(${lib} PRIVATE ${COSMO_STRICT_WARNINGS})
    add_dependencies(${lib} third_build)
endforeach()

#Collect all OBJECT outputs for linking
set(ALL_OBJECTS
    $<TARGET_OBJECTS:cosmo_util>
    $<TARGET_OBJECTS:cosmo_db>
    $<TARGET_OBJECTS:cosmo_platform>
    $<TARGET_OBJECTS:cosmo_mem>
    $<TARGET_OBJECTS:cosmo_linkage>
    $<TARGET_OBJECTS:cosmo_media>
    $<TARGET_OBJECTS:cosmo_infer>
    $<TARGET_OBJECTS:cosmo_network>
    $<TARGET_OBJECTS:cosmo_service>
    $<TARGET_OBJECTS:cosmo_flow>
    $<TARGET_OBJECTS:cosmo_api>
    $<TARGET_OBJECTS:cosmo_nn>
)

#Common link libraries(shared by main executable and tests)
set(COMMON_LIBS
    stdc++fs fmt glog cryptopp SQLiteCpp sqlite3 uSockets uuid z pcap
    openssl_ssl openssl_crypto curl
    event_core event_extra event
    mqtt_c mqtt_cs
    ffmpeg_avcodec ffmpeg_avdevice ffmpeg_avfilter
    ffmpeg_avformat ffmpeg_avutil ffmpeg_swresample ffmpeg_swscale
    mp4v2
    tokenizers_cpp tokenizers_c
    pthread dl m
)

#Backend - specific link libraries
if(COSMO_NN_USE_SOPHON_BACKEND)
    list(APPEND COMMON_LIBS bmlib bmrt bmcv bmvd bmvenc)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    list(APPEND COMMON_LIBS onnxruntime)
endif()

##########################################################
#Main Executable
##########################################################
set(EXECUTABLE_NAME "cosmo-engine")

add_executable(${EXECUTABLE_NAME} ${APP_SRC} ${ALL_OBJECTS})
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
# Third-party headers come from IMPORTED targets via COMMON_LIBS + header-only libs
target_link_libraries(${EXECUTABLE_NAME} PRIVATE nlohmann stb uWebSockets)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_directories(${EXECUTABLE_NAME} PRIVATE ${DEVICE_LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()
if(COSMO_MODEL_GUARD)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_HAS_MODEL_GUARD=1)
    if(MODEL_GUARD_LIB)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${MODEL_GUARD_LIB})
    endif()
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_NN_USE_CPU_BACKEND)
endif()
#Build order: OBJECT libs → third_build (explicit, see foreach above)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${COMMON_LIBS})

##########################################################
#Unit Tests(optional, cmake - DBUILD_TESTS = ON)
##########################################################
if(BUILD_TESTS)
    file(GLOB TEST_SRC "test/*.cc" "test/*.cpp" "test/mock/*.cc")

    #App sources without main.cc
    set(APP_SRC_NO_MAIN ${APP_SRC})
    list(FILTER APP_SRC_NO_MAIN EXCLUDE REGEX ".*app/main\\.cc$")

    add_executable(cosmo-tests ${APP_SRC_NO_MAIN} ${ALL_OBJECTS} ${TEST_SRC})
    add_dependencies(cosmo-tests third_build)
    target_include_directories(cosmo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/test)
    target_precompile_headers(cosmo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/test/catch_amalgamated.hpp
        ${CMAKE_SOURCE_DIR}/test/catch2/trompeloeil.hpp
    )
    target_link_libraries(cosmo-tests PRIVATE nlohmann stb uWebSockets)
    if(COSMO_NN_USE_SOPHON_BACKEND)
        target_link_directories(cosmo-tests PRIVATE ${DEVICE_LIB_DIR})
        target_compile_definitions(cosmo-tests PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
    endif()
    if(COSMO_MODEL_GUARD)
        target_compile_definitions(cosmo-tests PRIVATE COSMO_HAS_MODEL_GUARD=1)
        if(MODEL_GUARD_LIB)
            target_link_libraries(cosmo-tests PRIVATE ${MODEL_GUARD_LIB})
        endif()
    endif()
    if(COSMO_NN_USE_CPU_BACKEND)
        target_compile_definitions(cosmo-tests PRIVATE COSMO_NN_USE_CPU_BACKEND)
    endif()
    target_link_libraries(cosmo-tests PRIVATE ${COMMON_LIBS} gcov)

    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' '*/3rd/*' '*/test/*' '*/thirdparty_install/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_report
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report..."
    )
endif()




##########################################################
# Web Frontend Build (Vue 3 + Vite)
##########################################################
include(cmake/web_frontend.cmake)

##########################################################
# Install
##########################################################

# Binary
install(TARGETS ${EXECUTABLE_NAME}
    DESTINATION bin
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ GROUP_EXECUTE
)

# Nginx configuration (uses system nginx, no custom binary)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nginx/conf
    DESTINATION bin/nginx_conf
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Web Frontend
install(DIRECTORY ${WEB_BUILD_DIR}/web_unified/dist/
    DESTINATION web
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Data
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
    DESTINATION files
    FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
    PATTERN "*.json" EXCLUDE
    PATTERN "*.OTF"  EXCLUDE
    PATTERN "netplan" EXCLUDE
    PATTERN "resource" EXCLUDE
)
# Font
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/SOURCEHANSANSCN-REGULAR.OTF
    DESTINATION font
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
)

# Resource (models, algorithm, layout, etc. from external repository)
if(DEFINED RESOURCE_DIR AND IS_DIRECTORY "${RESOURCE_DIR}")
    message(STATUS "Resource directory: ${RESOURCE_DIR}")
    install(DIRECTORY ${RESOURCE_DIR}/
        DESTINATION resource
        FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
        PATTERN ".git" EXCLUDE
    )
else()
    message(STATUS "No resource directory specified — package will not contain models")
endif()

# Scripts (exclude dev-only tools from device deployment)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts/
    DESTINATION scripts
    FILES_MATCHING
        PATTERN "build*" EXCLUDE
        PATTERN "format_check*" EXCLUDE
        PATTERN "static_analysis*" EXCLUDE
        PATTERN "install-hooks*" EXCLUDE
        PATTERN "pre-commit" EXCLUDE
        PATTERN "*.sh"
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE
)

# Netplan failsafe config (deployed to scripts/ to keep device-side path unchanged)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/netplan/
    DESTINATION scripts
    FILES_MATCHING
        PATTERN "*.bak"
            PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ
)

##########################################################
# Packaging
##########################################################
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_major REGEX "kVersionMajor")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_minor REGEX "kVersionMinor")
file(STRINGS "${CMAKE_SOURCE_DIR}/src/util/Version.h" _ver_patch REGEX "kVersionPatch")
string(REGEX REPLACE ".*kVersionMajor = ([0-9]+).*" "\\1" VER_MAJOR "${_ver_major}")
string(REGEX REPLACE ".*kVersionMinor = ([0-9]+).*" "\\1" VER_MINOR "${_ver_minor}")
string(REGEX REPLACE ".*kVersionPatch = ([0-9]+).*" "\\1" VER_PATCH "${_ver_patch}")

set(PACKAGE_PREFIX  "cosmo")
set(PACKAGE_VERSION "V${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(PACKAGE_NAME    "${PACKAGE_PREFIX}-${PACKAGE_VERSION}")

# Generate and install version.txt
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.txt" "${PACKAGE_VERSION}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/version.txt"
    DESTINATION bin
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ
)

# Install model guard shared library to lib/ (used at runtime via dlopen or direct link)
if(COSMO_MODEL_GUARD AND MODEL_GUARD_LIB)
    install(FILES ${MODEL_GUARD_LIB}
        DESTINATION lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
else()
    # Create empty lib directory to satisfy legacy upgrade checks on target device
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
        DESTINATION .
    )
endif()

set(CPACK_PACKAGE_NAME              "${PACKAGE_PREFIX}")
set(CPACK_PACKAGE_VENDOR            "Nanjing Cosmo Wander AI Technology Co., Ltd.")
set(CPACK_PACKAGE_VERSION           "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${VER_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${VER_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${VER_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION       "Cosmo Package")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cosmo - AI Inference Engine")
set(CPACK_PACKAGE_CONTACT           "hello@cosmowander.ai")
set(CPACK_GENERATOR                 "TGZ")
set(CPACK_PACKAGE_FILE_NAME         "${PACKAGE_NAME}")
set(CPACK_OUTPUT_FILE_PREFIX        "${CMAKE_BINARY_DIR}/packages")

include(CPack)

add_custom_target(package_all
    COMMAND cpack
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/package_md5_rename.sh
        ${CMAKE_BINARY_DIR}/packages ${PACKAGE_NAME}
    DEPENDS ${EXECUTABLE_NAME} web_frontend
    COMMENT "Creating package with MD5 in filename"
)
