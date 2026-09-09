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

# Keep the existing numeric version formats while deriving a unique patch
# number from the nearest vX.Y.Z tag and the number of commits after it.
# Source archives without Git metadata retain the historical fallback version.
include("${CMAKE_SOURCE_DIR}/cmake/CosmoVersion.cmake")
cosmo_resolve_version("${CMAKE_SOURCE_DIR}" 1 1 0 0)
set(VER_MAJOR "${COSMO_VERSION_MAJOR}")
set(VER_MINOR "${COSMO_VERSION_MINOR}")
set(VER_PATCH "${COSMO_VERSION_PATCH}")
add_compile_definitions(
    COSMO_VERSION_MAJOR=${COSMO_VERSION_MAJOR}
    COSMO_VERSION_MINOR=${COSMO_VERSION_MINOR}
    COSMO_VERSION_PATCH=${COSMO_VERSION_PATCH}
    COSMO_VERSION_BUILD=${COSMO_VERSION_BUILD}
)
message(STATUS
    "Cosmo version: V${COSMO_VERSION_MAJOR}.${COSMO_VERSION_MINOR}."
    "${COSMO_VERSION_PATCH}.${COSMO_VERSION_BUILD} (${COSMO_VERSION_SOURCE})")

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
option(COSMO_ENABLE_COVERAGE "Enable gcov code-coverage instrumentation for the test build (adds --coverage; defines the 'coverage' lcov target)" OFF)
if(BUILD_TESTS AND COSMO_ENABLE_COVERAGE)
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
option(COSMO_NN_USE_RKNN_BACKEND "Enable Rockchip RKNN backend" OFF)

set(_cosmo_nn_backend_count 0)
foreach(_nn_backend_option
        COSMO_NN_USE_SOPHON_BACKEND
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_RKNN_BACKEND)
    if(${_nn_backend_option})
        math(EXPR _cosmo_nn_backend_count "${_cosmo_nn_backend_count} + 1")
    endif()
endforeach()
if(NOT _cosmo_nn_backend_count EQUAL 1)
    message(FATAL_ERROR "Exactly one NN backend must be enabled: Sophon, CPU, or RKNN")
endif()

# The selected inference backend is a build-wide compile contract. Business,
# API, and utility OBJECT libraries include the same backend-aware headers as
# the inference layer, so every project target must observe one consistent set
# of feature macros. Chip identity remains separate build data below.
if(COSMO_NN_USE_SOPHON_BACKEND)
    add_compile_definitions(COSMO_NN_USE_SOPHON_BACKEND)
elseif(COSMO_NN_USE_CPU_BACKEND)
    add_compile_definitions(
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_ONNX_BACKEND)
elseif(COSMO_NN_USE_RKNN_BACKEND)
    add_compile_definitions(
        COSMO_NN_USE_RKNN_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_RAW_MODEL_BACKEND)
endif()

# A target package is always chip-specific even when several chips share one
# inference/media implementation. Keep the chip identity as build data instead
# of spreading per-chip preprocessor branches through the RKNN backend.
set(COSMO_TARGET_CHIP "" CACHE STRING "Target accelerator chip recorded in release packages")
if(COSMO_TARGET_CHIP)
    string(TOLOWER "${COSMO_TARGET_CHIP}" COSMO_TARGET_CHIP_NORMALIZED)
    if(NOT COSMO_TARGET_CHIP_NORMALIZED MATCHES "^(bm1688|cv186x|rk3576|rv1126b|unspecified)$")
        message(FATAL_ERROR "Unsupported COSMO_TARGET_CHIP: ${COSMO_TARGET_CHIP}")
    endif()
endif()

include(cmake/CosmoRuntimePaths.cmake)
cosmo_configure_runtime_paths(
    "${COSMO_TARGET_CHIP_NORMALIZED}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/runtime-paths")
message(STATUS "Cosmo mutable data root: ${COSMO_DEFAULT_DATA_DIR}")

if(COSMO_NN_USE_RKNN_BACKEND)
    if(NOT COSMO_TARGET_CHIP_NORMALIZED MATCHES "^(rk3576|rv1126b)$")
        message(FATAL_ERROR
            "RKNN builds require COSMO_TARGET_CHIP=rk3576 or rv1126b")
    endif()
    string(TOUPPER "${COSMO_TARGET_CHIP_NORMALIZED}" COSMO_TARGET_CHIP_LABEL)
    set(COSMO_PLATFORM_PROFILE
        "${CMAKE_SOURCE_DIR}/config/rknn/platforms/${COSMO_TARGET_CHIP_NORMALIZED}.json")
    if(NOT EXISTS "${COSMO_PLATFORM_PROFILE}")
        message(FATAL_ERROR "RKNN platform profile not found: ${COSMO_PLATFORM_PROFILE}")
    endif()
    add_compile_definitions(
        COSMO_RKNN_TARGET_CHIP="${COSMO_TARGET_CHIP_NORMALIZED}"
        COSMO_RKNN_TARGET_CHIP_LABEL="${COSMO_TARGET_CHIP_LABEL}")
    message(STATUS
        "RKNN platform profile: ${COSMO_TARGET_CHIP_NORMALIZED} (${COSMO_PLATFORM_PROFILE})")
endif()

# Media acceleration is selected independently from inference. Defaults keep
# existing builds unchanged while allowing a software media pipeline to be
# paired with another inference backend.
set(_cosmo_media_sophon_default OFF)
set(_cosmo_media_cpu_default OFF)
set(_cosmo_media_rockchip_default OFF)
if(COSMO_NN_USE_SOPHON_BACKEND)
    set(_cosmo_media_sophon_default ON)
elseif(COSMO_NN_USE_RKNN_BACKEND AND
       (COSMO_ROCKCHIP_MEDIA_ROOT OR DEFINED ENV{ROCKCHIP_MEDIA_ROOT}))
    set(_cosmo_media_rockchip_default ON)
else()
    set(_cosmo_media_cpu_default ON)
endif()

option(COSMO_MEDIA_USE_SOPHON_BACKEND "Enable Sophon hardware media backend"
       ${_cosmo_media_sophon_default})
option(COSMO_MEDIA_USE_CPU_BACKEND "Enable FFmpeg software media backend"
       ${_cosmo_media_cpu_default})
option(COSMO_MEDIA_USE_ROCKCHIP_BACKEND "Enable Rockchip MPP/RGA hardware media backend"
       ${_cosmo_media_rockchip_default})

set(_cosmo_media_backend_count 0)
foreach(_media_backend_option
        COSMO_MEDIA_USE_SOPHON_BACKEND
        COSMO_MEDIA_USE_CPU_BACKEND
        COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    if(${_media_backend_option})
        math(EXPR _cosmo_media_backend_count "${_cosmo_media_backend_count} + 1")
    endif()
endforeach()
if(NOT _cosmo_media_backend_count EQUAL 1)
    message(FATAL_ERROR "Exactly one media backend must be enabled: Sophon, CPU, or Rockchip")
endif()
if(COSMO_MEDIA_USE_SOPHON_BACKEND AND NOT COSMO_NN_USE_SOPHON_BACKEND)
    message(FATAL_ERROR "Sophon media requires the Sophon device runtime")
endif()
if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND AND NOT COSMO_TARGET_ARCH STREQUAL "aarch64")
    message(FATAL_ERROR "Rockchip media requires an aarch64 target")
endif()

if(COSMO_MEDIA_USE_SOPHON_BACKEND)
    add_compile_definitions(COSMO_MEDIA_USE_SOPHON_BACKEND)
elseif(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    add_compile_definitions(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
else()
    add_compile_definitions(COSMO_MEDIA_USE_CPU_BACKEND)
endif()

set(_cosmo_openh264_default OFF)
if(COSMO_MEDIA_USE_CPU_BACKEND AND COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(_cosmo_openh264_default ON)
endif()
option(COSMO_ENABLE_OPENH264 "Require the bundled OpenH264 encoder" ${_cosmo_openh264_default})
set(COSMO_OPENH264_USE_ASM OFF)

set(COSMO_MODEL_GUARD OFF)

set(COSMO_MODEL_GUARD_BUILD_PROFILE "public-runtime" CACHE STRING
    "Model Guard build profile: public-runtime or production-release")
set_property(CACHE COSMO_MODEL_GUARD_BUILD_PROFILE PROPERTY STRINGS
    public-runtime production-release)
set(COSMO_PACKAGE_MODELS "include" CACHE STRING
    "Package preset models: include or preserve the installed model directory")
set_property(CACHE COSMO_PACKAGE_MODELS PROPERTY STRINGS include preserve)
if(NOT COSMO_PACKAGE_MODELS STREQUAL "include" AND
   NOT COSMO_PACKAGE_MODELS STREQUAL "preserve")
    message(FATAL_ERROR "COSMO_PACKAGE_MODELS must be include or preserve")
endif()
set(COSMO_MODEL_GUARD_BUILD_PROFILES public-runtime production-release)
if(NOT COSMO_MODEL_GUARD_BUILD_PROFILE IN_LIST
       COSMO_MODEL_GUARD_BUILD_PROFILES)
    message(FATAL_ERROR
        "Unsupported COSMO_MODEL_GUARD_BUILD_PROFILE="
        "${COSMO_MODEL_GUARD_BUILD_PROFILE}. Expected public-runtime or "
        "production-release.")
endif()

if(COSMO_NN_USE_SOPHON_BACKEND)
    set(COSMO_MODEL_GUARD ON)
    set(COSMO_MODEL_GUARD_PLATFORM "sophon")
elseif(COSMO_NN_USE_RKNN_BACKEND AND
       COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release" AND
       COSMO_TARGET_CHIP_NORMALIZED STREQUAL "rk3576")
    set(COSMO_MODEL_GUARD ON)
    set(COSMO_MODEL_GUARD_PLATFORM "rknn")
endif()

if(COSMO_MODEL_GUARD AND COSMO_MODEL_GUARD_PLATFORM STREQUAL "sophon")
    set(COSMO_MODEL_GUARD_SDK_ROOT
        "${CMAKE_SOURCE_DIR}/prebuild/model-guard-v2"
        CACHE PATH "Verified Cosmo Model Guard v2 SDK root")
    get_filename_component(COSMO_MODEL_GUARD_SDK_ROOT
        "${COSMO_MODEL_GUARD_SDK_ROOT}" ABSOLUTE)
    set(MODEL_GUARD_V2_HEADER
        "${COSMO_MODEL_GUARD_SDK_ROOT}/include/cosmo_model_guard_v2.h")
    set(MODEL_GUARD_V2_LIBRARY
        "${COSMO_MODEL_GUARD_SDK_ROOT}/lib/libcosmo_model_guard.so.2.0.0")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        set(MODEL_GUARD_V2_PROVISION_TOOL
            "${COSMO_MODEL_GUARD_SDK_ROOT}/bin/cosmo-model-provision")
    endif()
    set(MODEL_GUARD_V2_TEST_FIXTURE_MARKER
        "${COSMO_MODEL_GUARD_SDK_ROOT}/share/cosmo-model-guard/TEST_FIXTURE_DO_NOT_DEPLOY")

    set(COSMO_PYTHON3_EXECUTABLE "/usr/bin/python3")
    if(NOT EXISTS "${COSMO_PYTHON3_EXECUTABLE}" OR
       IS_DIRECTORY "${COSMO_PYTHON3_EXECUTABLE}" OR
       NOT CMAKE_READELF OR NOT EXISTS "${CMAKE_READELF}" OR
       NOT CMAKE_NM OR NOT EXISTS "${CMAKE_NM}")
        message(FATAL_ERROR
            "The protected Sophon build requires Python 3 and the target "
            "readelf/nm tools to verify the Model Guard v2 SDK")
    endif()

    set(MODEL_GUARD_V2_VERIFY_ARGUMENTS
        --admission-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}"
        --sdk-root "${COSMO_MODEL_GUARD_SDK_ROOT}"
        --readelf "${CMAKE_READELF}"
        --nm "${CMAKE_NM}")
    if(EXISTS "${MODEL_GUARD_V2_TEST_FIXTURE_MARKER}")
        message(FATAL_ERROR
            "A marked Model Guard test fixture cannot satisfy either public-runtime "
            "or production-release SDK admission")
    endif()

    set(MODEL_GUARD_V2_ADMISSION_INPUTS
        "${MODEL_GUARD_V2_HEADER}"
        "${MODEL_GUARD_V2_LIBRARY}"
        "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        list(APPEND MODEL_GUARD_V2_ADMISSION_INPUTS
            "${MODEL_GUARD_V2_PROVISION_TOOL}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${MODEL_GUARD_V2_ADMISSION_INPUTS})

    execute_process(
        COMMAND "${COSMO_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_v2_sdk.py"
                ${MODEL_GUARD_V2_VERIFY_ARGUMENTS}
        RESULT_VARIABLE MODEL_GUARD_V2_VERIFY_RESULT
        OUTPUT_VARIABLE MODEL_GUARD_V2_VERIFY_OUTPUT
        ERROR_VARIABLE MODEL_GUARD_V2_VERIFY_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT MODEL_GUARD_V2_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "COSMO_MODEL_GUARD requires a usable v2 SDK.\n"
            "${MODEL_GUARD_V2_VERIFY_ERROR}")
    endif()

    string(REGEX MATCH "(^|\n)verified_sdk_root=([^\n]+)"
        MODEL_GUARD_V2_VERIFIED_ROOT_LINE "${MODEL_GUARD_V2_VERIFY_OUTPUT}")
    set(MODEL_GUARD_V2_VERIFIED_SDK_ROOT "${CMAKE_MATCH_2}")
    if(NOT IS_ABSOLUTE "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}" OR
       NOT EXISTS "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}")
        message(FATAL_ERROR "Model Guard verifier did not return a valid SDK root")
    endif()
    set(MODEL_GUARD_V2_HEADER
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/include/cosmo_model_guard_v2.h")
    set(MODEL_GUARD_V2_LIBRARY
        "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/lib/libcosmo_model_guard.so.2.0.0")
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        set(MODEL_GUARD_V2_PROVISION_TOOL
            "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/bin/cosmo-model-provision")
    endif()
    add_library(cosmo_model_guard_v2 SHARED IMPORTED GLOBAL)
    set_target_properties(cosmo_model_guard_v2 PROPERTIES
        IMPORTED_LOCATION "${MODEL_GUARD_V2_LIBRARY}"
        IMPORTED_SONAME "libcosmo_model_guard.so.2"
        INTERFACE_INCLUDE_DIRECTORIES "${MODEL_GUARD_V2_VERIFIED_SDK_ROOT}/include"
    )
    message(STATUS "Verified Model Guard v2 SDK:\n${MODEL_GUARD_V2_VERIFY_OUTPUT}")
    message(STATUS
        "Model Guard build profile: ${COSMO_MODEL_GUARD_BUILD_PROFILE}")
    set(COSMO_MODEL_GUARD_LINK_TARGET cosmo_model_guard_v2)
endif()

if(COSMO_MODEL_GUARD AND COSMO_MODEL_GUARD_PLATFORM STREQUAL "rknn")
    set(COSMO_PYTHON3_EXECUTABLE "/usr/bin/python3")
    if(NOT EXISTS "${COSMO_PYTHON3_EXECUTABLE}" OR
       NOT CMAKE_READELF OR NOT EXISTS "${CMAKE_READELF}" OR
       NOT CMAKE_NM OR NOT EXISTS "${CMAKE_NM}")
        message(FATAL_ERROR
            "The protected RKNN build requires Python 3 and target readelf/nm")
    endif()
    set(COSMO_MODEL_GUARD_SDK_ROOT
        "${CMAKE_SOURCE_DIR}/prebuild/model-guard-v2-rknn-abi1"
        CACHE PATH "Verified Cosmo Model Guard v2.4 RKNN ABI 1 SDK root")
    get_filename_component(COSMO_MODEL_GUARD_SDK_ROOT
        "${COSMO_MODEL_GUARD_SDK_ROOT}" ABSOLUTE)
    set(MODEL_GUARD_RKNN_V1_HEADER
        "${COSMO_MODEL_GUARD_SDK_ROOT}/include/cosmo_model_guard_rknn_v1.h")
    set(MODEL_GUARD_RKNN_V1_LIBRARY
        "${COSMO_MODEL_GUARD_SDK_ROOT}/lib/libcosmo_model_guard_rknn.so.1.0.0")
    set(MODEL_GUARD_RKNN_V1_PROVISION_TOOL
        "${COSMO_MODEL_GUARD_SDK_ROOT}/bin/cosmo-model-provision")
    set(MODEL_GUARD_RKNN_V1_SDK_MANIFEST
        "${COSMO_MODEL_GUARD_SDK_ROOT}/SDK-MANIFEST.json")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${MODEL_GUARD_RKNN_V1_HEADER}"
        "${MODEL_GUARD_RKNN_V1_LIBRARY}"
        "${MODEL_GUARD_RKNN_V1_PROVISION_TOOL}"
        "${MODEL_GUARD_RKNN_V1_SDK_MANIFEST}"
        "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_rknn_v1_sdk.py")
    execute_process(
        COMMAND "${COSMO_PYTHON3_EXECUTABLE}" -I -B
                "${CMAKE_SOURCE_DIR}/scripts/verify_model_guard_rknn_v1_sdk.py"
                --admission-profile "${COSMO_MODEL_GUARD_BUILD_PROFILE}"
                --sdk-root "${COSMO_MODEL_GUARD_SDK_ROOT}"
                --readelf "${CMAKE_READELF}"
                --nm "${CMAKE_NM}"
        RESULT_VARIABLE MODEL_GUARD_RKNN_V1_VERIFY_RESULT
        OUTPUT_VARIABLE MODEL_GUARD_RKNN_V1_VERIFY_OUTPUT
        ERROR_VARIABLE MODEL_GUARD_RKNN_V1_VERIFY_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT MODEL_GUARD_RKNN_V1_VERIFY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "RKNN production build requires a usable Model Guard SDK.\n"
            "${MODEL_GUARD_RKNN_V1_VERIFY_ERROR}")
    endif()
    add_library(cosmo_model_guard_rknn_v1 SHARED IMPORTED GLOBAL)
    set_target_properties(cosmo_model_guard_rknn_v1 PROPERTIES
        IMPORTED_LOCATION "${MODEL_GUARD_RKNN_V1_LIBRARY}"
        IMPORTED_SONAME "libcosmo_model_guard_rknn.so.1"
        INTERFACE_INCLUDE_DIRECTORIES "${COSMO_MODEL_GUARD_SDK_ROOT}/include")
    set(COSMO_MODEL_GUARD_LINK_TARGET cosmo_model_guard_rknn_v1)
    message(STATUS "Verified Model Guard v2.4 RKNN SDK:\n${MODEL_GUARD_RKNN_V1_VERIFY_OUTPUT}")
endif()

if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
    if(NOT COSMO_MODEL_GUARD)
        message(FATAL_ERROR
            "COSMO_MODEL_GUARD_BUILD_PROFILE=production-release requires the "
            "protected AArch64 build")
    endif()
    if(NOT COSMO_TARGET_ARCH STREQUAL "aarch64" OR
       NOT (COSMO_NN_USE_SOPHON_BACKEND OR COSMO_NN_USE_RKNN_BACKEND))
        message(FATAL_ERROR
            "The protected build is valid only for a supported AArch64 backend")
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

# Third-party projects must not inherit wall-clock or ephemeral container
# identity in release artifacts.
set(COSMO_REPRODUCIBLE_BUILD_EPOCH 1784776233)
set(COSMO_REPRODUCIBLE_BUILD_UTC "2026-07-23 03:10:33")
set(COSMO_REPRODUCIBLE_BUILD_UNAME
    "Linux cosmo-build 0.0.0 #1 SMP ${COSMO_TARGET_ARCH} GNU/Linux")

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
include(cmake/cryptopp.cmake)
include(cmake/curl.cmake)
include(cmake/mqtt.cmake)
include(cmake/event.cmake)
include(cmake/ffmpeg.cmake)
include(cmake/uWebSockets.cmake)
include(cmake/tokenizers.cmake)
if(COSMO_NN_USE_SOPHON_BACKEND)
    include(cmake/device.cmake)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    include(cmake/onnxruntime.cmake)
endif()
if(COSMO_NN_USE_RKNN_BACKEND)
    include(cmake/rknn.cmake)
    include(cmake/rkllm.cmake)
endif()
if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    include(cmake/rockchip_media.cmake)
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
if(COSMO_NN_USE_RKNN_BACKEND)
    list(APPEND COMMON_LIBS rknnrt)
endif()
if(COSMO_NN_USE_RKLLM_BACKEND)
    list(APPEND COMMON_LIBS rkllmrt)
endif()
if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    list(APPEND COMMON_LIBS rockchip_mpp rockchip_rga)
endif()

##########################################################
#Main Executable
##########################################################
set(EXECUTABLE_NAME "cosmo-engine")

add_executable(${EXECUTABLE_NAME} ${APP_SRC} ${ALL_OBJECTS})
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${COSMO_RUNTIME_PATHS_INCLUDE_DIR})
# Third-party headers come from IMPORTED targets via COMMON_LIBS + header-only libs
target_link_libraries(${EXECUTABLE_NAME} PRIVATE nlohmann stb uWebSockets)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_directories(${EXECUTABLE_NAME} PRIVATE ${DEVICE_LIB_DIR})
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()
if(COSMO_MODEL_GUARD)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE COSMO_HAS_MODEL_GUARD=1)
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${COSMO_MODEL_GUARD_LINK_TARGET})
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_ONNX_BACKEND)
endif()
if(COSMO_NN_USE_RKNN_BACKEND)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
        COSMO_NN_USE_RKNN_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_RAW_MODEL_BACKEND)
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        BUILD_RPATH "${COSMO_RKNN_ROOT}/lib"
        INSTALL_RPATH "$ORIGIN/../lib")
endif()
if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    set_property(TARGET ${EXECUTABLE_NAME} APPEND PROPERTY
        BUILD_RPATH "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib")
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib")
endif()
#Build order: OBJECT libs → third_build (explicit, see foreach above)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE ${COMMON_LIBS})

if(COSMO_NN_USE_RKNN_BACKEND)
    add_executable(cosmo-rknn-backend-smoke
        tools/rknn/cosmo_rknn_backend_smoke.cc
        ${ALL_OBJECTS})
    target_include_directories(cosmo-rknn-backend-smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(cosmo-rknn-backend-smoke PRIVATE
        COSMO_NN_USE_RKNN_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_RAW_MODEL_BACKEND)
    target_link_libraries(cosmo-rknn-backend-smoke PRIVATE
        nlohmann stb uWebSockets ${COMMON_LIBS})
    if(COSMO_MODEL_GUARD)
        target_link_libraries(cosmo-rknn-backend-smoke PRIVATE ${COSMO_MODEL_GUARD_LINK_TARGET})
    endif()
    set_target_properties(cosmo-rknn-backend-smoke PROPERTIES
        BUILD_RPATH "${COSMO_RKNN_ROOT}/lib")
    if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
        set_property(TARGET cosmo-rknn-backend-smoke APPEND PROPERTY
            BUILD_RPATH "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib")

        add_executable(cosmo-rknn-fastpath-qualify
            tools/rknn/cosmo_rknn_fastpath_qualify.cc
            ${ALL_OBJECTS})
        target_include_directories(cosmo-rknn-fastpath-qualify PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_compile_definitions(cosmo-rknn-fastpath-qualify PRIVATE
            COSMO_NN_USE_RKNN_BACKEND
            COSMO_NN_USE_HOST_BACKEND
            COSMO_NN_USE_RAW_MODEL_BACKEND)
        target_link_libraries(cosmo-rknn-fastpath-qualify PRIVATE
            nlohmann stb uWebSockets ${COMMON_LIBS})
        if(COSMO_MODEL_GUARD)
            target_link_libraries(cosmo-rknn-fastpath-qualify PRIVATE ${COSMO_MODEL_GUARD_LINK_TARGET})
        endif()
        set_target_properties(cosmo-rknn-fastpath-qualify PROPERTIES
            BUILD_RPATH "${COSMO_RKNN_ROOT}/lib;${COSMO_ROCKCHIP_MEDIA_ROOT}/lib")
    endif()
endif()

##########################################################
#Unit Tests(optional, cmake - DBUILD_TESTS = ON)
##########################################################
if(BUILD_TESTS)
    file(GLOB TEST_SRC "test/*.cc" "test/*.cpp" "test/mock/*.cc")
    list(FILTER TEST_SRC EXCLUDE REGEX
        ".*ReleaseBootstrapVerifierStandaloneTest\\.cc$")

    #App sources without main.cc
    set(APP_SRC_NO_MAIN ${APP_SRC})
    list(FILTER APP_SRC_NO_MAIN EXCLUDE REGEX ".*app/main\\.cc$")

    add_executable(cosmo-tests ${APP_SRC_NO_MAIN} ${ALL_OBJECTS} ${TEST_SRC})
    add_dependencies(cosmo-tests third_build)
    target_compile_definitions(cosmo-tests PRIVATE CATCH_AMALGAMATED_CUSTOM_MAIN)
    target_include_directories(cosmo-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test
        ${COSMO_RUNTIME_PATHS_INCLUDE_DIR})
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
        target_link_libraries(cosmo-tests PRIVATE ${COSMO_MODEL_GUARD_LINK_TARGET})
    endif()
    if(COSMO_NN_USE_CPU_BACKEND)
        target_compile_definitions(cosmo-tests PRIVATE
            COSMO_NN_USE_CPU_BACKEND
            COSMO_NN_USE_HOST_BACKEND
            COSMO_NN_USE_ONNX_BACKEND)
    endif()
    if(COSMO_NN_USE_RKNN_BACKEND)
        target_compile_definitions(cosmo-tests PRIVATE
            COSMO_NN_USE_RKNN_BACKEND
            COSMO_NN_USE_HOST_BACKEND
            COSMO_NN_USE_RAW_MODEL_BACKEND)
        set_target_properties(cosmo-tests PROPERTIES
            BUILD_RPATH "${COSMO_RKNN_ROOT}/lib")
    endif()
    if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
        set_property(TARGET cosmo-tests APPEND PROPERTY
            BUILD_RPATH "${COSMO_ROCKCHIP_MEDIA_ROOT}/lib")
    endif()
    target_link_libraries(cosmo-tests PRIVATE ${COMMON_LIBS})
    if(COSMO_ENABLE_COVERAGE)
        # gcov runtime is only needed when the test binary was instrumented.
        target_link_libraries(cosmo-tests PRIVATE gcov)

        add_custom_target(coverage
            COMMAND lcov --capture --directory . --output-file coverage.info
            COMMAND lcov --remove coverage.info '/usr/*' '*/3rd/*' '*/test/*' '*/thirdparty_install/*' --output-file coverage.info
            COMMAND genhtml coverage.info --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report..."
        )
    endif()
endif()




##########################################################
# Web Frontend Build (Vue 3 + Vite)
##########################################################
include(cmake/web_frontend.cmake)

##########################################################
# Install
##########################################################

# Public binary archives are distributions of the Apache-2.0 work, not just
# deployment payloads. Keep the project LICENSE/NOTICE at the archive root and
# ship the exact license texts for the runtime dependencies inventoried by
# NOTICE. Platform-specific SDK/model licenses are installed by their own
# packaging paths below.
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE
    DESTINATION .
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE
    DESTINATION share/licenses/cosmo-edge
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
)

function(cosmo_install_runtime_license component source_path)
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR
            "Runtime license source is missing for ${component}: ${source_path}")
    endif()
    install(FILES "${source_path}"
        DESTINATION "share/licenses/third-party/${component}"
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
endfunction()

cosmo_install_runtime_license(cryptopp
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/cryptopp-CRYPTOPP_8_9_0/License.txt")
cosmo_install_runtime_license(cryptopp-cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/cryptopp-cmake-CRYPTOPP_8_9_0/LICENSE")
cosmo_install_runtime_license(curl
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/curl-8.17.0/COPYING")
cosmo_install_runtime_license(eigen
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/eigen3-3.2.92/LICENSE.MPL2.0")
cosmo_install_runtime_license(ffmpeg
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/srs-6.0-r0/trunk/3rdparty/ffmpeg-4-fit/LICENSE.md")
cosmo_install_runtime_license(ffmpeg
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/third-party/ffmpeg/COPYING.LGPLv2.1")
cosmo_install_runtime_license(fmt
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/fmt-7.1.2/LICENSE.rst")
cosmo_install_runtime_license(glog
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/glog/COPYING")
cosmo_install_runtime_license(libevent
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/libevent-2.1.12-stable/LICENSE")
cosmo_install_runtime_license(libsophon
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/libsophon-0.4.11/LICENSE")
cosmo_install_runtime_license(libuuid
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/libuuid-1.0.3/COPYING")
cosmo_install_runtime_license(mp4v2
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/mp4v2-2.0.0/COPYING")
cosmo_install_runtime_license(onnxruntime
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/onnxruntime-linux-x64-1.26.0/LICENSE")
cosmo_install_runtime_license(onnxruntime
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/onnxruntime-linux-x64-1.26.0/ThirdPartyNotices.txt")
cosmo_install_runtime_license(openssl
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/openssl-3.5.3/LICENSE.txt")
cosmo_install_runtime_license(paho-mqtt-c
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/paho.mqtt.c-1.3.15/LICENSE")
cosmo_install_runtime_license(paho-mqtt-c
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/paho.mqtt.c-1.3.15/NOTICE")
cosmo_install_runtime_license(srs
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/srs-6.0-r0/LICENSE")
cosmo_install_runtime_license(sqlitecpp
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/SQLiteCpp-3.3.3/LICENSE.txt")
cosmo_install_runtime_license(tokenizers-cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/tokenizers-cpp/LICENSE")
cosmo_install_runtime_license(usockets
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/uSockets/LICENSE")
cosmo_install_runtime_license(uwebsockets
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/uWebSockets/LICENSE")
cosmo_install_runtime_license(zlib
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/zlib-1.3.1/LICENSE")

if(COSMO_NN_USE_RKNN_BACKEND)
    cosmo_install_runtime_license(rknn-runtime
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/third-party/rknn-runtime/LICENSE")
    cosmo_install_runtime_license(rockchip-mpp
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/third-party/rockchip-mpp/Apache-2.0")
    cosmo_install_runtime_license(rockchip-mpp
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/third-party/rockchip-mpp/MIT")
    cosmo_install_runtime_license(rockchip-librga
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/third-party/rockchip-librga/COPYING")
endif()

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
    PATTERN "test-video" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "*.pyo" EXCLUDE
)

# Package-specific runtime roots. Launchers treat these as defaults and retain
# explicit COSMO_DATA_DIR/COSMO_APP_DATA_DIR overrides.
install(FILES "${COSMO_RUNTIME_PATHS_ENV}"
    DESTINATION share/cosmo
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
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
        PATTERN "models" EXCLUDE
        PATTERN "model-artifacts" EXCLUDE
        PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE PATTERN "*.pyo" EXCLUDE
    )
    if(DEFINED RESOURCE_OVERLAY_DIR AND IS_DIRECTORY "${RESOURCE_OVERLAY_DIR}")
        message(STATUS "Resource overlay directory: ${RESOURCE_OVERLAY_DIR}")
        set(COSMO_RESOURCE_OVERLAY_MODEL_BUNDLE_ARGS)
        if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
            list(APPEND COSMO_RESOURCE_OVERLAY_MODEL_BUNDLE_ARGS
                PATTERN "model-bundle.json" EXCLUDE)
        endif()
        install(DIRECTORY ${RESOURCE_OVERLAY_DIR}/
            DESTINATION resource
            FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
            PATTERN ".git" EXCLUDE
            PATTERN "models" EXCLUDE
            PATTERN "model-artifacts" EXCLUDE
            ${COSMO_RESOURCE_OVERLAY_MODEL_BUNDLE_ARGS}
            PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE PATTERN "*.pyo" EXCLUDE
        )
    endif()
    if(COSMO_PACKAGE_MODELS STREQUAL "include" AND
       DEFINED RESOURCE_MODELS_DIR AND IS_DIRECTORY "${RESOURCE_MODELS_DIR}")
        message(STATUS "Resource model directory: ${RESOURCE_MODELS_DIR}")
        install(DIRECTORY ${RESOURCE_MODELS_DIR}/
            DESTINATION resource/models
            FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
            PATTERN ".git" EXCLUDE
            PATTERN "model.nn" PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
            PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE PATTERN "*.pyo" EXCLUDE
        )
    elseif(COSMO_PACKAGE_MODELS STREQUAL "include" AND IS_DIRECTORY "${RESOURCE_DIR}/models")
        install(DIRECTORY ${RESOURCE_DIR}/models/
            DESTINATION resource/models
            FILE_PERMISSIONS OWNER_WRITE OWNER_READ GROUP_WRITE GROUP_READ
            PATTERN ".git" EXCLUDE
            PATTERN "model.nn" PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
            PATTERN "__pycache__" EXCLUDE PATTERN "*.pyc" EXCLUDE PATTERN "*.pyo" EXCLUDE
        )
    endif()
else()
    message(STATUS "No resource directory specified — package will not contain models")
endif()

if(COSMO_NN_USE_RKNN_BACKEND)
    install(FILES "${RKNN_RUNTIME_LIBRARY}"
        DESTINATION lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
    if(COSMO_NN_USE_RKLLM_BACKEND)
        install(FILES "${RKLLM_RUNTIME_LIBRARY}"
            DESTINATION lib
            PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
        )
        install(FILES "${RKLLM_RUNTIME_LICENSE}"
            DESTINATION share/licenses/rkllm
            RENAME LICENSE
            PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
        )
    endif()
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
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "*.pyo" EXCLUDE
        PATTERN "package_md5_rename.sh" EXCLUDE
        PATTERN "restore-symlinks.sh" EXCLUDE
        PATTERN "sync-source-volume.sh" EXCLUDE
        PATTERN "legacy_migration_install.sh" EXCLUDE
        PATTERN "install.sh" EXCLUDE
        PATTERN "start.sh" EXCLUDE
        PATTERN "inte_run_start.sh" EXCLUDE
        PATTERN "*.sh"
            PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE
)

# Open and Protected editions deliberately share one permanent MD5 upgrade
# lifecycle. The Protected edition differs only in its guarded model runtime
# and provisioning tool; application archives are not signed.
install(PROGRAMS
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/legacy_migration_install.sh
    DESTINATION scripts RENAME install.sh
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE)
install(PROGRAMS
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/start.sh
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inte_run_start.sh
    DESTINATION scripts
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE)

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
if(COSMO_NN_USE_RKNN_BACKEND AND COSMO_PACKAGE_MODELS STREQUAL "include")
    if(NOT DEFINED RESOURCE_OVERLAY_DIR OR NOT RESOURCE_OVERLAY_DIR)
        message(FATAL_ERROR
            "RKNN packages that include models require RESOURCE_OVERLAY_DIR")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(verify_rknn_packaging_resources ALL
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/rknn/stage_platform_resources.py"
            --platform-profile "${COSMO_PLATFORM_PROFILE}"
            --output-dir "${RESOURCE_OVERLAY_DIR}"
            --verify
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Verifying target-specific RKNN resource identity and freshness"
        VERBATIM
    )
endif()

set(PACKAGE_PREFIX  "cosmo")
set(PACKAGE_VERSION "V${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
set(PACKAGE_NAME    "${PACKAGE_PREFIX}-${PACKAGE_VERSION}")

# Record the intended accelerator inside the archive. The exported sidecar is
# convenient for automation, but it can be separated from the package during a
# manual copy. This marker keeps such mistakes observable and prevents packages
# for different chips from being byte-identical.
if(COSMO_TARGET_CHIP)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/target-chip.txt"
        "${COSMO_TARGET_CHIP_NORMALIZED}\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/target-chip.txt"
        DESTINATION share/cosmo
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
    message(STATUS "Package target chip: ${COSMO_TARGET_CHIP_NORMALIZED}")
endif()

if(COSMO_NN_USE_RKNN_BACKEND)
    install(FILES "${COSMO_PLATFORM_PROFILE}"
        DESTINATION share/cosmo
        RENAME platform-profile.json
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
endif()

# Generate and install version.txt
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.txt" "${PACKAGE_VERSION}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/version.txt"
    DESTINATION bin
    PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
)

# Install the exact verified compatibility member. The linker aliases are
# recreated inside the package; no unversioned host/system library is copied.
if(COSMO_MODEL_GUARD AND COSMO_MODEL_GUARD_PLATFORM STREQUAL "sophon")
    install(FILES "${MODEL_GUARD_V2_LIBRARY}"
        DESTINATION lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    )
    if(COSMO_MODEL_GUARD_BUILD_PROFILE STREQUAL "production-release")
        install(PROGRAMS "${MODEL_GUARD_V2_PROVISION_TOOL}"
            DESTINATION bin
            RENAME cosmo-model-provision
            PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
        )
    endif()
    install(FILES "${MODEL_GUARD_V2_HEADER}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/prebuild/model-guard-v2/README.md"
        DESTINATION share/licenses/cosmo-model-guard
        RENAME ARTIFACT-TERMS.md
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    install(CODE [[
        set(model_guard_lib_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib")
        file(CREATE_LINK libcosmo_model_guard.so.2.0.0
             "${model_guard_lib_dir}/libcosmo_model_guard.so.2"
             SYMBOLIC RESULT model_guard_soname_link_result)
        if(NOT model_guard_soname_link_result STREQUAL "0")
            message(FATAL_ERROR
                "Cannot create Model Guard SONAME link: ${model_guard_soname_link_result}")
        endif()
        file(CREATE_LINK libcosmo_model_guard.so.2
             "${model_guard_lib_dir}/libcosmo_model_guard.so"
             SYMBOLIC RESULT model_guard_link_link_result)
        if(NOT model_guard_link_link_result STREQUAL "0")
            message(FATAL_ERROR
                "Cannot create Model Guard linker alias: ${model_guard_link_link_result}")
        endif()
    ]])
elseif(COSMO_MODEL_GUARD AND COSMO_MODEL_GUARD_PLATFORM STREQUAL "rknn")
    file(SHA256 "${MODEL_GUARD_RKNN_V1_LIBRARY}" MODEL_GUARD_RKNN_V1_LIBRARY_SHA256)
    set(MODEL_GUARD_RKNN_V1_HASH_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/model-guard-rknn-runtime.sha256")
    file(WRITE "${MODEL_GUARD_RKNN_V1_HASH_FILE}"
        "${MODEL_GUARD_RKNN_V1_LIBRARY_SHA256}  lib/libcosmo_model_guard_rknn.so.1.0.0\n")
    install(FILES "${MODEL_GUARD_RKNN_V1_LIBRARY}"
        DESTINATION lib
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
    install(PROGRAMS "${MODEL_GUARD_RKNN_V1_PROVISION_TOOL}"
        DESTINATION bin RENAME cosmo-model-provision
        PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    install(FILES "${MODEL_GUARD_RKNN_V1_HEADER}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
    install(FILES "${MODEL_GUARD_RKNN_V1_HASH_FILE}"
        DESTINATION share/cosmo-model-guard RENAME runtime.sha256
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
    install(FILES "${MODEL_GUARD_RKNN_V1_SDK_MANIFEST}"
        DESTINATION share/cosmo-model-guard
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
    install(FILES "${COSMO_MODEL_GUARD_SDK_ROOT}/README.md"
        DESTINATION share/licenses/cosmo-model-guard RENAME ARTIFACT-TERMS.md
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
    install(CODE [[
        set(model_guard_lib_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib")
        file(CREATE_LINK libcosmo_model_guard_rknn.so.1.0.0
             "${model_guard_lib_dir}/libcosmo_model_guard_rknn.so.1"
             SYMBOLIC RESULT model_guard_soname_link_result)
        if(NOT model_guard_soname_link_result STREQUAL "0")
            message(FATAL_ERROR "Cannot create RKNN Model Guard SONAME link")
        endif()
        file(CREATE_LINK libcosmo_model_guard_rknn.so.1
             "${model_guard_lib_dir}/libcosmo_model_guard_rknn.so"
             SYMBOLIC RESULT model_guard_link_result)
        if(NOT model_guard_link_result STREQUAL "0")
            message(FATAL_ERROR "Cannot create RKNN Model Guard linker alias")
        endif()
    ]])
else()
    # CPU-only development packages retain the historical empty directory.
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

set(COSMO_PACKAGE_TARGETS ${EXECUTABLE_NAME} web_frontend)
if(TARGET verify_rknn_packaging_resources)
    list(APPEND COSMO_PACKAGE_TARGETS verify_rknn_packaging_resources)
endif()
add_custom_target(package_all
    # CPack names are content-addressed only after generation. Clear artifacts
    # from an earlier profile before creating and exporting this build's output.
    COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "${CMAKE_BINARY_DIR}/packages"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_BINARY_DIR}/packages"
    COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C.UTF-8 cpack
    COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/package_md5_rename.sh"
        "${CMAKE_BINARY_DIR}/packages" "${PACKAGE_NAME}"
        "${COSMO_MODEL_GUARD_BUILD_PROFILE}"
        "${COSMO_REPRODUCIBLE_BUILD_EPOCH}"
    DEPENDS ${COSMO_PACKAGE_TARGETS}
    COMMENT "Creating MD5-compatible upgrade package"
)
