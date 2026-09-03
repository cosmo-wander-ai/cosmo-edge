# src/nn/CMakeLists.txt
# NN source code — compiled as OBJECT library, linked into main executable

set(NN_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ── Sophon SDK path (used only for Sophon backend) ──
if(COSMO_NN_USE_SOPHON_BACKEND)
    set(SOPHON_ROOT_DIR ${DEVICE_ROOT_DIR})
endif()

# ── Collect NN source files ──
file(GLOB_RECURSE NN_ALL_SRC
    "${NN_DIR}/core/*.h"   "${NN_DIR}/core/*.cc"
    "${NN_DIR}/guard/*.h"  "${NN_DIR}/guard/*.cc"
    "${NN_DIR}/node/*.h"   "${NN_DIR}/node/*.cc"
    "${NN_DIR}/pipeline/*.h" "${NN_DIR}/pipeline/*.cc"
    "${NN_DIR}/utils/*.h"  "${NN_DIR}/utils/*.cc"
)

# ── Device-specific sources ──
# Always include naive device (host memory management)
file(GLOB_RECURSE NN_NAIVE_SRC
    "${NN_DIR}/device/naive/*.h" "${NN_DIR}/device/naive/*.cc"
)
list(APPEND NN_ALL_SRC ${NN_NAIVE_SRC})

if(COSMO_NN_USE_SOPHON_BACKEND)
    file(GLOB_RECURSE NN_SOPHON_SRC
        "${NN_DIR}/device/sophon/*.h" "${NN_DIR}/device/sophon/*.cc"
    )
    list(APPEND NN_ALL_SRC ${NN_SOPHON_SRC})
endif()

if(COSMO_NN_USE_CPU_BACKEND OR COSMO_NN_USE_RKNN_BACKEND)
    file(GLOB_RECURSE NN_CPU_SRC
        "${NN_DIR}/device/cpu/*.h" "${NN_DIR}/device/cpu/*.cc"
    )
    file(GLOB_RECURSE NN_HOST_SRC
        "${NN_DIR}/device/host/*.h" "${NN_DIR}/device/host/*.cc"
    )
    list(APPEND NN_ALL_SRC ${NN_CPU_SRC} ${NN_HOST_SRC})
endif()

if(COSMO_NN_USE_RKNN_BACKEND)
    file(GLOB_RECURSE NN_RKNN_SRC
        "${NN_DIR}/device/rknn/*.h" "${NN_DIR}/device/rknn/*.cc"
    )
    list(APPEND NN_ALL_SRC ${NN_RKNN_SRC})
endif()

# ── Build cosmo_nn OBJECT library ──
add_library(cosmo_nn OBJECT ${NN_ALL_SRC})

target_include_directories(cosmo_nn PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# ── Backend-specific configuration ──
target_compile_definitions(cosmo_nn PRIVATE COSMO_NN_NAIVE_ENABLE)

if(COSMO_NN_USE_SOPHON_BACKEND)
    target_compile_definitions(cosmo_nn PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
    # Sophon headers provided by bmlib IMPORTED target
endif()

if(COSMO_MODEL_GUARD)
    target_compile_definitions(cosmo_nn PRIVATE COSMO_HAS_MODEL_GUARD=1)
    target_link_libraries(cosmo_nn PRIVATE ${COSMO_MODEL_GUARD_LINK_TARGET})
endif()

if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(cosmo_nn PRIVATE
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_ONNX_BACKEND)
    # ORT headers provided by onnxruntime IMPORTED target
endif()

if(COSMO_NN_USE_RKNN_BACKEND)
    target_compile_definitions(cosmo_nn PRIVATE
        COSMO_NN_USE_RKNN_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_RAW_MODEL_BACKEND)
endif()

# Symbol hiding + suppress C++11-era warnings
target_compile_options(cosmo_nn PRIVATE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -Wno-deprecated-declarations
    -Wno-ignored-attributes
)

# Header-only and third-party dependencies
target_link_libraries(cosmo_nn PRIVATE eigen3 cosmo_common tokenizers_cpp tokenizers_c)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_nn PRIVATE bmlib bmrt bmcv bmmodel bmvd bmvenc)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_link_libraries(cosmo_nn PRIVATE onnxruntime)
endif()
if(COSMO_NN_USE_RKNN_BACKEND)
    target_link_libraries(cosmo_nn PRIVATE rknnrt)
    if(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
        target_link_libraries(cosmo_nn PRIVATE rockchip_rga)
    endif()
endif()

# Ensure all ExternalProject third-party targets are built before compiling NN
add_dependencies(cosmo_nn third_build)
