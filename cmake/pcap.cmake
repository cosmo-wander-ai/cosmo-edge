# set(PCAP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/libpcap-1.10.6)
# set(PCAP_INSTALL_DIR ${THIRDPARTY_INSTALL_PREFIX}/pcap)
# set(PCAP_HEADERS ${GLOG_INSTALL_DIR}/include)
# set(PCAP_LIB ${GLOG_INSTALL_DIR}/lib/libpcap.so)

# ExternalProject_Add(
#     pcap_external

#     SOURCE_DIR ${PCAP_SOURCE_DIR}

#     CONFIGURE_COMMAND <SOURCE_DIR>/configure
#         --prefix=${PCAP_INSTALL_DIR}
#         --host=aarch64-linux-gnu
#         --with-pcap=linux
    
#     BUILD_COMMAND $(MAKE)
#     INSTALL_COMMAND $(MAKE) install

#     UPDATE_COMMAND ""
#     BUILD_ALWAYS OFF

#     LOG_CONFIGURE ON
#     LOG_BUILD ON
#     LOG_INSTALL ON
#     LOG_OUTPUT_ON_FAILURE ON
# )

# add_dependencies(third_build pcap_external)

# include_directories(${PCAP_HEADERS})

# add_library(pcre SHARED IMPORTED)
# set_target_properties(pcre PROPERTIES
#                     IMPORTED_LOCATION ${PCAP_LIB})

# install(DIRECTORY ${PCAP_INSTALL_DIR}/lib/
#         DESTINATION lib
#         FILES_MATCHING
#             PATTERN "*so*")

set(PCAP_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd/pcap)
set(PCAP_HEADERS ${PCAP_ROOT_DIR}/include)

if(COSMO_TARGET_ARCH STREQUAL "aarch64")
    set(PCAP_LIB_DIR ${PCAP_ROOT_DIR}/lib)
elseif(COSMO_TARGET_ARCH STREQUAL "x86_64")
    set(PCAP_LIB_DIR ${PCAP_ROOT_DIR}/lib64)
endif()

set(PCAP_LIB ${PCAP_LIB_DIR}/libpcap.a)
if(NOT EXISTS ${PCAP_LIB})
    message(FATAL_ERROR "pcap library not found: ${PCAP_LIB}")
endif()

add_library(pcap STATIC IMPORTED)
set_target_properties(pcap PROPERTIES
    IMPORTED_LOCATION ${PCAP_LIB}
    INTERFACE_INCLUDE_DIRECTORIES "${PCAP_HEADERS}"
)
