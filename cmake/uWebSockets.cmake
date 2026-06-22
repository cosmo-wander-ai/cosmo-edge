add_library(uWebSockets INTERFACE IMPORTED)
set_target_properties(uWebSockets PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/3rd/uWebSockets/src"
)