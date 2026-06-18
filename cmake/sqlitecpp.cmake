##########################################################
# SQLiteCpp — modern C++ SQLite wrapper (from 3rd/)
#
# Uses SQLiteCpp's bundled sqlite3 amalgamation so we can
# drop the old sqlite-amalgamation-3510100 directory.
##########################################################

set(SQLITECPP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/SQLiteCpp-3.3.3)
set(SQLITECPP_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/sqlitecpp)
set(SQLITECPP_HEADERS ${SQLITECPP_INSTALL_DIR}/include)
set(SQLITECPP_LIB ${SQLITECPP_INSTALL_DIR}/lib/libSQLiteCpp.a)
set(SQLITE3_LIB ${SQLITECPP_INSTALL_DIR}/lib/libsqlite3.a)

ExternalProject_Add(
    sqlitecpp_external

    SOURCE_DIR ${SQLITECPP_SOURCE_DIR}

    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${SQLITECPP_INSTALL_DIR}
        -DSQLITECPP_INTERNAL_SQLITE=ON
        -DSQLITECPP_BUILD_TESTS=OFF
        -DSQLITECPP_BUILD_EXAMPLES=OFF
        -DSQLITECPP_RUN_CPPLINT=OFF
        -DSQLITECPP_RUN_CPPCHECK=OFF
        -DSQLITECPP_RUN_DOXYGEN=OFF
        -DSQLITECPP_USE_STACK_PROTECTION=OFF

    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install

    UPDATE_COMMAND ""
    BUILD_ALWAYS OFF

    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON
)

add_dependencies(third_build sqlitecpp_external)

# SQLiteCpp static library (depends on sqlite3)
add_library(SQLiteCpp STATIC IMPORTED)
set_target_properties(SQLiteCpp PROPERTIES
    IMPORTED_LOCATION ${SQLITECPP_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${SQLITECPP_HEADERS}"
)
add_dependencies(SQLiteCpp sqlitecpp_external)

# Bundled sqlite3 static library
add_library(sqlite3 STATIC IMPORTED)
set_target_properties(sqlite3 PROPERTIES
    IMPORTED_LOCATION ${SQLITE3_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${SQLITECPP_HEADERS}"
)
add_dependencies(sqlite3 sqlitecpp_external)
