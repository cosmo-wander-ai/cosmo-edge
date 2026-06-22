file(GLOB_RECURSE FLOW_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

add_library(cosmo_flow OBJECT ${FLOW_SRC})

# Project headers
target_include_directories(cosmo_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_flow PRIVATE cosmo_common
    SQLiteCpp sqlite3 mp4v2
    ffmpeg_avcodec ffmpeg_avformat ffmpeg_avutil
)

# Backend compile definitions
# (AiDetector uses the flag to keep CPU/x86 trial behavior separate from Sophon production)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_compile_definitions(cosmo_flow PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(cosmo_flow PRIVATE COSMO_NN_USE_CPU_BACKEND)
endif()
