# FFmpeg prebuilt binary integration
#
# Instead of compiling FFmpeg from source via ExternalProject_Add, we use
# prebuilt shared libraries placed under prebuild/ffmpeg/<arch>/.
#
# Prerequisites:
#   - prebuild/ffmpeg/aarch64/{include/,lib/} for the Sophon/aarch64 target
#   - prebuild/ffmpeg/x86_64/{include/,lib/}  for the CPU/x86_64 target
#   - All .so soname symlinks must be present (e.g. libavcodec.so -> libavcodec.so.58)
#   - If COSMO_ENABLE_OPENH264 is ON, libopenh264.so must also be present in the
#     same lib/ directory (FFmpeg must have been built with --enable-libopenh264).
#
# License rationale: using prebuilt binaries with known configure flags (no GPL
# components) eliminates the risk of accidentally enabling GPL-only encoders or
# decoders during a developer's local source build.

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    set(FFMPEG_PREBUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prebuild/ffmpeg/aarch64)
elseif(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(FFMPEG_PREBUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prebuild/ffmpeg/x86_64)
else()
    message(FATAL_ERROR "Unsupported architecture for FFmpeg prebuilt: ${COSMO_TARGET_ARCH}")
endif()

set(FFMPEG_HEADERS         ${FFMPEG_PREBUILD_DIR}/include)
set(FFMPEG_AVCODEC_LIB     ${FFMPEG_PREBUILD_DIR}/lib/libavcodec.so)
set(FFMPEG_AVDEVICE_LIB    ${FFMPEG_PREBUILD_DIR}/lib/libavdevice.so)
set(FFMPEG_AVFILTER_LIB    ${FFMPEG_PREBUILD_DIR}/lib/libavfilter.so)
set(FFMPEG_AVFORMAT_LIB    ${FFMPEG_PREBUILD_DIR}/lib/libavformat.so)
set(FFMPEG_AVUTIL_LIB      ${FFMPEG_PREBUILD_DIR}/lib/libavutil.so)
set(FFMPEG_SWRESAMPLE_LIB  ${FFMPEG_PREBUILD_DIR}/lib/libswresample.so)
set(FFMPEG_SWSCALE_LIB     ${FFMPEG_PREBUILD_DIR}/lib/libswscale.so)

message(STATUS "FFmpeg: using prebuilt libraries from ${FFMPEG_PREBUILD_DIR}")

# ── OpenH264 (optional, required when COSMO_ENABLE_OPENH264=ON) ──────────────
if(COSMO_ENABLE_OPENH264)
    set(OPENH264_LIBRARY ${FFMPEG_PREBUILD_DIR}/lib/libopenh264.so)
    if(NOT EXISTS "${OPENH264_LIBRARY}")
        message(FATAL_ERROR
            "COSMO_ENABLE_OPENH264=ON but prebuilt libopenh264.so not found: "
            "${OPENH264_LIBRARY}\n"
            "Place a prebuilt libopenh264.so (built with matching FFmpeg) in "
            "${FFMPEG_PREBUILD_DIR}/lib/")
    endif()
    message(STATUS "FFmpeg OpenH264: using prebuilt ${OPENH264_LIBRARY}")

    add_library(openh264 SHARED IMPORTED)
    set_target_properties(openh264 PROPERTIES
        IMPORTED_LOCATION              ${OPENH264_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES  "${FFMPEG_HEADERS}"
    )
endif()

# ── FFmpeg IMPORTED targets ───────────────────────────────────────────────────
# Target names are identical to the previous ExternalProject-based setup so
# that all consumers (src/media/CMakeLists.txt, CMakeLists.txt COMMON_LIBS)
# require no changes.

add_library(ffmpeg_avcodec SHARED IMPORTED)
set_target_properties(ffmpeg_avcodec PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_AVCODEC_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)
if(COSMO_ENABLE_OPENH264)
    target_link_libraries(ffmpeg_avcodec INTERFACE openh264)
endif()

add_library(ffmpeg_avdevice SHARED IMPORTED)
set_target_properties(ffmpeg_avdevice PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_AVDEVICE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

add_library(ffmpeg_avfilter SHARED IMPORTED)
set_target_properties(ffmpeg_avfilter PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_AVFILTER_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

add_library(ffmpeg_avformat SHARED IMPORTED)
set_target_properties(ffmpeg_avformat PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_AVFORMAT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

add_library(ffmpeg_avutil SHARED IMPORTED)
set_target_properties(ffmpeg_avutil PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_AVUTIL_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

add_library(ffmpeg_swresample SHARED IMPORTED)
set_target_properties(ffmpeg_swresample PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_SWRESAMPLE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

add_library(ffmpeg_swscale SHARED IMPORTED)
set_target_properties(ffmpeg_swscale PROPERTIES
    IMPORTED_LOCATION             ${FFMPEG_SWSCALE_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_HEADERS}"
)

# ── Install: copy prebuilt .so files (with soname symlinks) into package ──────
install(DIRECTORY ${FFMPEG_PREBUILD_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*.so*"
)
