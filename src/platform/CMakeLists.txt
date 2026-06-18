add_library(cosmo_platform OBJECT
    NetCardOp.cc          NetCardOp.h
    NetCardOpConfig.cc
    NetCardOpInternal.h
    SystemReboot.cc       SystemReboot.h
    SystemTime.cc         SystemTime.h
    WatchDog.cc           WatchDog.h
)

target_include_directories(cosmo_platform PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_platform PRIVATE cosmo_common)

if(COSMO_NN_USE_SOPHON_BACKEND)
    target_compile_definitions(cosmo_platform PRIVATE COSMO_NN_USE_SOPHON_BACKEND)
endif()
if(COSMO_NN_USE_CPU_BACKEND)
    target_compile_definitions(cosmo_platform PRIVATE COSMO_NN_USE_CPU_BACKEND)
endif()
