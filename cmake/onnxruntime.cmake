# cmake/onnxruntime.cmake
# ONNX Runtime — prebuilt shared library (CPU backend only)
#
# Download the prebuilt package and extract to 3rd/onnxruntime/:
#   wget https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-1.26.0.tgz
#   tar xzf onnxruntime-linux-x64-1.26.0.tgz
#   mv onnxruntime-linux-x64-1.26.0 3rd/onnxruntime

set(ORT_ROOT_DIR ${CMAKE_SOURCE_DIR}/3rd/onnxruntime-linux-x64-1.26.0)
set(ORT_HEADERS ${ORT_ROOT_DIR}/include)
set(ORT_LIB ${ORT_ROOT_DIR}/lib/libonnxruntime.so)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ORT_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_HEADERS}"
)

install(DIRECTORY ${ORT_ROOT_DIR}/lib/
    DESTINATION lib
    FILES_MATCHING
        PATTERN "*so*"
)
