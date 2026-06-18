#############################################################################
####filename:--->CMakeLists.txt_binary_exec                              ####
####function:--->cmake file template for                                 ####
####			 generating binary execuatable file                      ####
####author	:--->shenhm                                                  ####
####created	:--->2020.04.24                                              ####
####version	:--->v1.0                                                    ####
#############################################################################
#project name
PROJECT(uSockets C)

#requirement of cmake version
CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

# for uSocket
add_definitions(-DLIBUS_NO_SSL)

#header include file path, similar to -I/usr/include in Makefile
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)

FILE(GLOB SOCKET_LIST ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
FILE(GLOB SOCKET_CRYPTO_LIST ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/*.c)
FILE(GLOB SOCKET_EVENTING_LIST ${CMAKE_CURRENT_SOURCE_DIR}/src/eventing/*.c)

set(USOCKET_HEADERS src/libusockets.h)

add_library(usockets SHARED ${SOCKET_LIST} ${SOCKET_CRYPTO_LIST} ${SOCKET_EVENTING_LIST})

install(TARGETS usockets DESTINATION lib)
install(FILES ${USOCKET_HEADERS} DESTINATION include)




