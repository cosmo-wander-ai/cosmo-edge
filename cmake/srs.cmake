set(SRS_ORIGINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/srs-6.0-r0/trunk)
set(SRS_SOURCE_DIR ${CMAKE_BINARY_DIR}/srs_source)
set(SRS_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/srs)
set(SRS_DOWNLOAD_COMMAND
    ${CMAKE_COMMAND} -E rm -rf <SOURCE_DIR>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SRS_ORIGINAL_SOURCE_DIR} <SOURCE_DIR>
)
set(SRS_GB28181_PATCH_COMMAND
    ${CMAKE_COMMAND}
        "-DSRS_SOURCE_DIR=<SOURCE_DIR>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_srs_gb28181.cmake
)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    set(SRS_PATCH_COMMAND
        bash ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_srs_crossbuild.sh <SOURCE_DIR>
        COMMAND ${SRS_GB28181_PATCH_COMMAND}
    )
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
    set(SRS_PATCH_COMMAND ${SRS_GB28181_PATCH_COMMAND})
    set(SRS_CONFIGURE_ARCH_ARGS
        --cross=off
        --cc=${CMAKE_C_COMPILER}
        --cxx=${CMAKE_CXX_COMPILER}
        --arch=x86_64
    )
endif()

# SRS uses its own ./configure && make build system (not CMake).
# Build artifacts go into a private source copy under the build directory because
# SRS does not reliably support true out-of-source builds (internal scripts use
# relative paths). Keeping the copy outside 3rd/ also lets us apply compatibility
# patches without modifying vendored sources.
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
        --gb28181=on
        --ffmpeg-fit=on
        --sanitizer=off
        --nasm=off
        --srtp-nasm=off
        --utest=off
        --jobs=4
        COMMAND ${CMAKE_COMMAND}
            "-DSRS_AUTO_HEADERS=<SOURCE_DIR>/objs/srs_auto_headers.hpp"
            "-DSRS_BUILD_EPOCH=${COSMO_REPRODUCIBLE_BUILD_EPOCH}"
            "-DSRS_BUILD_DATE=${COSMO_REPRODUCIBLE_BUILD_UTC}"
            "-DSRS_BUILD_UNAME=${COSMO_REPRODUCIBLE_BUILD_UNAME}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/normalize_srs_build_metadata.cmake

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

# Install a runtime template so SRS logs and its PID follow the package-specific
# mutable data root and any explicit COSMO_DATA_DIR override.
install(FILES ${CMAKE_CURRENT_LIST_DIR}/srs.conf.in
    DESTINATION bin/srs_conf
    RENAME srs.conf)
