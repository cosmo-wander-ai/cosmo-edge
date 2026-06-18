# Common sources (always compiled)
set(MEM_COMMON_SOURCES
    BlockFreqCalc.cc     BlockFreqCalc.h
    FixedBlockPool.cc    FixedBlockPool.h
    MemoryPoolMng.cc     MemoryPoolMng.h
    Allocator.h
    Block.h
    IDeviceContext.h
    DeviceContext.h
)

# Backend-specific sources — CMake selects which files to compile
if(COSMO_NN_USE_SOPHON_BACKEND)
    set(MEM_BACKEND_SOURCES
        AllocatorSophon.cc   AllocatorSophon.h
        DeviceContextSophon.cc
    )
else()
    set(MEM_BACKEND_SOURCES
        AllocatorCpu.cc      AllocatorCpu.h
        DeviceContextCpu.cc
    )
endif()

add_library(cosmo_mem OBJECT ${MEM_COMMON_SOURCES} ${MEM_BACKEND_SOURCES})

# Project headers (mem/ and util/ via src/)
target_include_directories(cosmo_mem PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_mem PRIVATE cosmo_common)
if(COSMO_NN_USE_SOPHON_BACKEND)
    target_link_libraries(cosmo_mem PRIVATE bmlib)
endif()
