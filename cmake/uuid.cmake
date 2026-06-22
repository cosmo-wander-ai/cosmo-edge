set(UUID_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libuuid-1.0.3)
set(UUID_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/uuid)
set(UUID_HEADERS ${UUID_INSTALL_DIR}/include)
set(UUID_LIB ${UUID_INSTALL_DIR}/lib/libuuid.so)

ExternalProject_Add(
    uuid_external

    SOURCE_DIR ${UUID_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${UUID_INSTALL_DIR}
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build uuid_external)

add_library(uuid SHARED IMPORTED)
set_target_properties(uuid PROPERTIES
    IMPORTED_LOCATION ${UUID_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${UUID_HEADERS}"
)
add_dependencies(uuid uuid_external)

install(DIRECTORY ${UUID_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)