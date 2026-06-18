set(OPENSSL_ORIGINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openssl-3.5.3)
set(OPENSSL_SOURCE_DIR ${OPENSSL_ORIGINAL_SOURCE_DIR})
set(OPENSSL_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/openssl)
set(OPENSSL_HEADERS ${OPENSSL_INSTALL_DIR}/include)
set(OPENSSL_SSL_LIB ${OPENSSL_INSTALL_DIR}/lib/libssl.so)
set(OPENSSL_CRYPTO_LIB ${OPENSSL_INSTALL_DIR}/lib/libcrypto.so)
set(OPENSSL_DOWNLOAD_COMMAND "")
set(OPENSSL_PATCH_COMMAND ${CMAKE_COMMAND} -E true)

if(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(OPENSSL_SOURCE_DIR ${CMAKE_BINARY_DIR}/openssl_source)
    set(OPENSSL_DOWNLOAD_COMMAND
        ${CMAKE_COMMAND} -E rm -rf <SOURCE_DIR>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${OPENSSL_ORIGINAL_SOURCE_DIR} <SOURCE_DIR>
    )
    set(OPENSSL_PATCH_COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/cmake/clean_openssl_source.sh <SOURCE_DIR>)
endif()

set(OPENSSL_COMMON_CONFIGURE_ARGS
    --prefix=${OPENSSL_INSTALL_DIR}
    --openssldir=/usr/local/ssl
    --libdir=lib
    --release
    --api=1.0.2
    shared
    zlib-dynamic
    no-apps
    no-docs
    no-tests
)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    set(OPENSSL_CONFIGURE_COMMAND
        <SOURCE_DIR>/config linux-aarch64
        ${OPENSSL_COMMON_CONFIGURE_ARGS}
        --cross-compile-prefix=aarch64-linux-gnu-
    )
elseif(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(OPENSSL_CONFIGURE_COMMAND
        <SOURCE_DIR>/config linux-x86_64
        ${OPENSSL_COMMON_CONFIGURE_ARGS}
    )
endif()

ExternalProject_Add(
    openssl_external
    
    SOURCE_DIR ${OPENSSL_SOURCE_DIR}
    DOWNLOAD_COMMAND ${OPENSSL_DOWNLOAD_COMMAND}
    PATCH_COMMAND ${OPENSSL_PATCH_COMMAND}

    CONFIGURE_COMMAND ${OPENSSL_CONFIGURE_COMMAND}
    
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install_sw
    
    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build openssl_external)

add_library(openssl_ssl SHARED IMPORTED)
set_target_properties(openssl_ssl PROPERTIES
    IMPORTED_LOCATION ${OPENSSL_SSL_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_HEADERS}"
)
add_dependencies(openssl_ssl openssl_external)

add_library(openssl_crypto SHARED IMPORTED)
set_target_properties(openssl_crypto PROPERTIES
    IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_HEADERS}"
)
add_dependencies(openssl_crypto openssl_external)

install(DIRECTORY ${OPENSSL_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)
