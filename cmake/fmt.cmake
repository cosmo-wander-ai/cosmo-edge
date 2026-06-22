set(FMT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/fmt-7.1.2)
set(FMT_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/fmt)
set(FMT_HEADERS ${FMT_INSTALL_DIR}/include)
set(FMT_LIB ${FMT_INSTALL_DIR}/lib/libfmt.so)

ExternalProject_Add(
    fmt_external

    SOURCE_DIR ${FMT_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${FMT_INSTALL_DIR}
        -DBUILD_SHARED_LIBS=ON
        -DFMT_DOC=OFF
        -DFMT_TEST=OFF
    
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build fmt_external)

add_library(fmt SHARED IMPORTED)
set_target_properties(fmt PROPERTIES
    IMPORTED_LOCATION ${FMT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${FMT_HEADERS}"
)
add_dependencies(fmt fmt_external)

install(DIRECTORY ${FMT_INSTALL_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)