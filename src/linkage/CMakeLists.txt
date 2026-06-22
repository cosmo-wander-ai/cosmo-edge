add_library(cosmo_linkage OBJECT
    LinkAgeAlarm.cc       LinkAgeAlarm.h
    LinkAgeAudioDevice.cc LinkAgeAudioDevice.h
    LinkAgeBase.cc        LinkAgeBase.h
    LinkAgeBaseCommon.h
    LinkAgeTask.cc        LinkAgeTask.h

    LinkAgeBaseCommon.cc
)

# Project headers (linkage/ references util/, service/ via src/)
target_include_directories(cosmo_linkage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(cosmo_linkage PRIVATE cosmo_common)
