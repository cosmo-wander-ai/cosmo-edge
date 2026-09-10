add_library(cosmo_platform OBJECT
    NetCardOp.cc          NetCardOp.h
    NetCardOpConfig.cc
    NetworkManagerConfig.h
    NetCardOpInternal.h
    SystemReboot.cc       SystemReboot.h
    SystemTime.cc         SystemTime.h
    WatchDog.cc           WatchDog.h
)

target_include_directories(cosmo_platform PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_platform PRIVATE cosmo_common)
