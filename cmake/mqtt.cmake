set(MQTT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/paho.mqtt.c-1.3.15)
set(MQTT_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/mqtt)
set(MQTT_HEADERS ${MQTT_INSTALL_DIR}/include)
set(MQTT_C_LIB ${MQTT_INSTALL_DIR}/lib/libpaho-mqtt3c.so)
set(MQTT_CS_LIB ${MQTT_INSTALL_DIR}/lib/libpaho-mqtt3cs.so)

ExternalProject_Add(
    mqtt_external

    SOURCE_DIR ${MQTT_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${MQTT_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DOPENSSL_ROOT_DIR=${THIRDPARTY_INSTALL_PREFIX}/openssl
        -DOPENSSL_INCLUDE_DIR=${THIRDPARTY_INSTALL_PREFIX}/openssl/include
        -DOPENSSL_SSL_LIBRARY=${THIRDPARTY_INSTALL_PREFIX}/openssl/lib/libssl.so
        -DOPENSSL_CRYPTO_LIBRARY=${THIRDPARTY_INSTALL_PREFIX}/openssl/lib/libcrypto.so
        -DPAHO_WITH_SSL=ON
        -DPAHO_WITH_LIBUUID=OFF
        -DPAHO_ENABLE_TESTING=OFF
        -DPAHO_BUILD_SAMPLES=OFF
        -DPAHO_BUILD_DEB_PACKAGE=OFF
        -DPAHO_BUILD_DOCUMENTATION=OFF
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    DEPENDS openssl_external

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build mqtt_external)

add_library(mqtt_c SHARED IMPORTED)
set_target_properties(mqtt_c PROPERTIES
    IMPORTED_LOCATION ${MQTT_C_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${MQTT_HEADERS}"
)
add_dependencies(mqtt_c mqtt_external)

add_library(mqtt_cs SHARED IMPORTED)
set_target_properties(mqtt_cs PROPERTIES
    IMPORTED_LOCATION ${MQTT_CS_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${MQTT_HEADERS}"
)
add_dependencies(mqtt_cs mqtt_external)

install(DIRECTORY ${MQTT_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*c.so*"
        PATTERN "*cs.so*"
)
