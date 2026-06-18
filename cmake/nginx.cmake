set(NGINX_ORIGINAL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/nginx-release-1.28.1)
set(NGINX_SOURCE_DIR ${NGINX_ORIGINAL_SOURCE_DIR})
set(NGINX_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/nginx)
set(NGINX_DOWNLOAD_COMMAND "")
set(NGINX_PATCH_COMMAND ${CMAKE_COMMAND} -E true)
set(NGINX_PCRE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/pcre2-pcre2-10.47)
set(NGINX_ZLIB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/zlib-1.3.1)
set(NGINX_OPENSSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openssl-3.5.3)

if(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(NGINX_SOURCE_DIR ${CMAKE_BINARY_DIR}/nginx_source)
    set(NGINX_PCRE_SOURCE_DIR ${CMAKE_BINARY_DIR}/nginx_pcre2_source)
    set(NGINX_ZLIB_SOURCE_DIR ${CMAKE_BINARY_DIR}/nginx_zlib_source)
    set(NGINX_OPENSSL_SOURCE_DIR ${CMAKE_BINARY_DIR}/nginx_openssl_source)
    set(NGINX_DOWNLOAD_COMMAND
        ${CMAKE_COMMAND} -E rm -rf <SOURCE_DIR>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${NGINX_ORIGINAL_SOURCE_DIR} <SOURCE_DIR>
    )
    set(NGINX_PATCH_COMMAND
        sh ${CMAKE_CURRENT_SOURCE_DIR}/cmake/prepare_nginx_deps.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/3rd/pcre2-pcre2-10.47
            ${NGINX_PCRE_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/3rd/zlib-1.3.1
            ${NGINX_ZLIB_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openssl-3.5.3
            ${NGINX_OPENSSL_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/clean_openssl_source.sh
    )
endif()

ExternalProject_Add(
    nginx_external

    SOURCE_DIR ${NGINX_SOURCE_DIR}
    DOWNLOAD_COMMAND ${NGINX_DOWNLOAD_COMMAND}
    PATCH_COMMAND ${NGINX_PATCH_COMMAND}

    CONFIGURE_COMMAND <SOURCE_DIR>/auto/configure
        --prefix=${NGINX_INSTALL_DIR}
        --add-module=${CMAKE_CURRENT_SOURCE_DIR}/3rd/nginx-http-flv-module-1.2.12
        --with-pcre=${NGINX_PCRE_SOURCE_DIR}
        --with-zlib=${NGINX_ZLIB_SOURCE_DIR}
        --with-openssl=${NGINX_OPENSSL_SOURCE_DIR}
        --with-cc=${CMAKE_C_COMPILER}
    
    BUILD_COMMAND $(MAKE)
    BUILD_IN_SOURCE ON
    INSTALL_COMMAND $(MAKE) install

    UPDATE_COMMAND ""
    BUILD_ALWAYS ON

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build nginx_external)

install(PROGRAMS ${NGINX_SOURCE_DIR}/objs/nginx
    DESTINATION bin
)
