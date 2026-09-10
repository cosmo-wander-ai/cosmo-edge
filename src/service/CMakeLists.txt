file(GLOB_RECURSE SERVICE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

add_library(cosmo_service OBJECT ${SERVICE_SRC})

# Project headers
target_include_directories(cosmo_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_service PRIVATE cosmo_common
    SQLiteCpp sqlite3 cryptopp pcap curl cosmo_xml
    uWebSockets uSockets z openssl_ssl openssl_crypto
    ffmpeg_avcodec ffmpeg_avformat ffmpeg_avutil
    event_core event_extra event
)
# Backend compile definitions and link libraries
# (MemoryPoolServiceImpl needs allocator selection, HardwareQueryUtil needs device info,
#  NnBackendConstants.h needs backend selection for directory prefix / engine type)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_service PRIVATE bmlib)
    target_compile_definitions(cosmo_service PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()

if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(cosmo_service PRIVATE
        COSMO_NN_USE_CPU_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_ONNX_BACKEND)
endif()

if(COSMO_NN_USE_RKNN_BACKEND)
    target_link_libraries(cosmo_service PRIVATE rknnrt)
    target_compile_definitions(cosmo_service PRIVATE
        COSMO_NN_USE_RKNN_BACKEND
        COSMO_NN_USE_HOST_BACKEND
        COSMO_NN_USE_RAW_MODEL_BACKEND)
endif()
