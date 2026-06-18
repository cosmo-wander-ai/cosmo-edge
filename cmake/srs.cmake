set(SRS_ORIGINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/srs-6.0-r0/trunk)
set(SRS_SOURCE_DIR ${SRS_ORIGINAL_SOURCE_DIR})
set(SRS_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/srs)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    set(SRS_DOWNLOAD_COMMAND "")
    set(SRS_PATCH_COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_srs_crossbuild.sh <SOURCE_DIR>)
    set(SRS_CONFIGURE_ARCH_ARGS
        --cross=on
        --cc=${CMAKE_C_COMPILER}
        --cxx=${CMAKE_CXX_COMPILER}
        --ar=aarch64-linux-gnu-ar
        --ld=aarch64-linux-gnu-ld
        --randlib=aarch64-linux-gnu-ranlib
        --arch=aarch64
        --host=aarch64-linux-gnu
        --cross-prefix=aarch64-linux-gnu-
    )
elseif(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(SRS_SOURCE_DIR ${CMAKE_BINARY_DIR}/srs_source)
    set(SRS_DOWNLOAD_COMMAND
        ${CMAKE_COMMAND} -E rm -rf <SOURCE_DIR>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SRS_ORIGINAL_SOURCE_DIR} <SOURCE_DIR>
    )
    set(SRS_PATCH_COMMAND ${CMAKE_COMMAND} -E true)
    set(SRS_CONFIGURE_ARCH_ARGS
        --cross=off
        --cc=${CMAKE_C_COMPILER}
        --cxx=${CMAKE_CXX_COMPILER}
        --arch=x86_64
    )
endif()

# SRS uses its own ./configure && make build system (not CMake).
# Build artifacts go into ${SRS_SOURCE_DIR}/objs/ as SRS does not reliably
# support true out-of-source builds (internal scripts use relative paths).
# The objs/ directory is already covered by 3rd/srs-6.0-r0/.gitignore.
ExternalProject_Add(
    srs_external

    SOURCE_DIR ${SRS_SOURCE_DIR}
    DOWNLOAD_COMMAND ${SRS_DOWNLOAD_COMMAND}

    # Patch: bypass native tool checks (g++, unzip, pkg-config) that are
    # irrelevant for cross-compilation. The Docker build env only has the
    # aarch64 cross-toolchain, not all native host tools.
    PATCH_COMMAND ${SRS_PATCH_COMMAND}

    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --prefix=${SRS_INSTALL_DIR}
        ${SRS_CONFIGURE_ARCH_ARGS}
        --srt=off
        --rtc=on
        --h265=on
        --ffmpeg-fit=on
        --sanitizer=off
        --nasm=off
        --srtp-nasm=off
        --utest=off
        --jobs=4

    BUILD_COMMAND $(MAKE)
    BUILD_IN_SOURCE ON
    INSTALL_COMMAND $(MAKE) install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build srs_external)

# Install SRS binary
install(PROGRAMS ${SRS_SOURCE_DIR}/objs/srs DESTINATION bin)

# Generate and install SRS configuration inline (no external file dependency).
# Uses bracket argument [=[...]=] to preserve semicolons verbatim.
file(WRITE ${CMAKE_BINARY_DIR}/srs.conf [=[
# SRS configuration for Cosmo device deployment.
# @see full.conf for all available options.

listen              1936;
max_connections     1000;
daemon              on;
pid                 /data/cwaiuserdata/log/logs/srs.pid;
srs_log_tank        file;
srs_log_file        /data/cwaiuserdata/log/logs/srs.log;

http_api {
    enabled         on;
    listen          1985;
}
http_server {
    enabled         on;
    listen          18088;
    dir             ./objs/nginx/html;
}
rtc_server {
    enabled on;
    listen 8000;
    candidate *;
}
vhost __defaultVhost__ {
    hls {
        enabled     on;
    }
    http_remux {
        enabled     on;
        mount       [vhost]/[app]/[stream].flv;
    }
    rtc {
        enabled     on;
        rtmp_to_rtc on;
        rtc_to_rtmp on;
    }
    play {
        gop_cache_max_frames 2500;
    }
}
]=])
install(FILES ${CMAKE_BINARY_DIR}/srs.conf DESTINATION bin/srs_conf)
