# Pinned, static XML parser for ONVIF. Keep downloaded sources in the build tree.
include(FetchContent)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cosmo_tinyxml2
    URL https://codeload.github.com/leethomason/tinyxml2/tar.gz/refs/tags/10.0.0
    URL_HASH SHA256=3bdf15128ba16686e69bce256cc468e76c7b94ff2c7f391cc5ec09e40bff3839
)
FetchContent_GetProperties(cosmo_tinyxml2)
if(NOT cosmo_tinyxml2_POPULATED)
    FetchContent_Populate(cosmo_tinyxml2)
endif()
add_library(cosmo_xml STATIC ${cosmo_tinyxml2_SOURCE_DIR}/tinyxml2.cpp)
target_include_directories(cosmo_xml SYSTEM PUBLIC ${cosmo_tinyxml2_SOURCE_DIR})
set_target_properties(cosmo_xml PROPERTIES POSITION_INDEPENDENT_CODE ON)
install(FILES ${cosmo_tinyxml2_SOURCE_DIR}/LICENSE.txt DESTINATION licenses/tinyxml2)
