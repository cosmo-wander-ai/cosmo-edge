set(Z_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/zlib-1.3.1)
set(Z_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/zlib)
set(Z_HEADERS ${Z_INSTALL_DIR}/include)
set(Z_LIB ${Z_INSTALL_DIR}/lib/libz.so)

ExternalProject_Add(
    z_external

    SOURCE_DIR ${Z_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${Z_INSTALL_DIR}
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_dependencies(third_build z_external)

add_library(z SHARED IMPORTED)
set_target_properties(z PROPERTIES
    IMPORTED_LOCATION ${Z_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${Z_HEADERS}"
)
add_dependencies(z z_external)

install(DIRECTORY ${Z_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)
