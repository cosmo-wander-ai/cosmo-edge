set(MP4V2_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/mp4v2-2.0.0)
set(MP4V2_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/mp4v2)
set(MP4V2_HEADERS ${MP4V2_INSTALL_DIR}/include)
set(MP4V2_LIB ${MP4V2_INSTALL_DIR}/lib/libmp4v2.so)

set(MP4V2_CONFIGURE_ARGS
    --prefix=${MP4V2_INSTALL_DIR}
    --disable-debug
    --disable-util
    --enable-shared=yes
    --enable-static=no
    CC=${CMAKE_C_COMPILER}
    CXX=${CMAKE_CXX_COMPILER}
)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    list(APPEND MP4V2_CONFIGURE_ARGS --host=aarch64-linux-gnu)
endif()

ExternalProject_Add(
    mp4v2_external

    SOURCE_DIR ${MP4V2_SOURCE_DIR}

    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        ${MP4V2_CONFIGURE_ARGS}
    
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build mp4v2_external)

add_library(mp4v2 SHARED IMPORTED)
set_target_properties(mp4v2 PROPERTIES
    IMPORTED_LOCATION ${MP4V2_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${MP4V2_HEADERS}"
)
add_dependencies(mp4v2 mp4v2_external)

install(DIRECTORY ${MP4V2_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)

