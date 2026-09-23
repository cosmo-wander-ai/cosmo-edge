set(CURL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/curl-8.17.0)
set(CURL_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/curl)
set(CURL_HEADERS ${CURL_INSTALL_DIR}/include)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CURL_LIB ${CURL_INSTALL_DIR}/lib/libcurl-d.so)
else()
    set(CURL_LIB ${CURL_INSTALL_DIR}/lib/libcurl.so)
endif()
set(CURL_EXTERNAL_DEPENDS openssl_external)

ExternalProject_Add(
    curl_external

    SOURCE_DIR ${CURL_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
        -DOPENSSL_ROOT_DIR=${OPENSSL_INSTALL_DIR}
        -DOPENSSL_INCLUDE_DIR=${OPENSSL_HEADERS}
        -DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIB}
        -DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIB}
        -DBUILD_SHARED_LIBS=ON
        -DCURL_USE_LIBPSL=OFF
        # Cross-compilation skips curl's host CA auto-detection. This path is
        # resolved on the target at runtime and must be provided by the image.
        -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
        -DCURL_CA_PATH=none
        -DBUILD_LIBCURL_DOCS=OFF
        -DBUILD_MISC_DOCS=OFF
        -DENABLE_CURL_MANUAL=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_CURL_EXE=OFF
        -DBUILD_EXAMPLES=OFF
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    DEPENDS ${CURL_EXTERNAL_DEPENDS}

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build curl_external)

add_library(curl SHARED IMPORTED)
set_target_properties(curl PROPERTIES
    IMPORTED_LOCATION ${CURL_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${CURL_HEADERS}"
)
add_dependencies(curl curl_external)

install(DIRECTORY ${CURL_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*curl*"
        PATTERN "*so*"
)
