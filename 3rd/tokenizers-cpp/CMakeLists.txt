cmake_minimum_required(VERSION 3.18)
project(tokenizers_cpp C CXX)

# update to contain more rust flags
set(TOKENIZERS_CPP_RUST_FLAGS "")
set(TOKENIZERS_CPP_CARGO_TARGET "")

# extra link libraries
set(TOKENIZERS_CPP_LINK_LIBS "")
set(TOKENIZERS_C_LINK_LIBS "")
set(CARGO_EXTRA_ENVS "")
message(STATUS "system-name" ${CMAKE_SYSTEM_NAME})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND TOKENIZERS_C_LINK_LIBS ${CMAKE_DL_LIBS})
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(TOKENIZERS_CPP_CARGO_TARGET aarch64-unknown-linux-gnu)
  endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(TOKENIZERS_CPP_CARGO_TARGET wasm32-unknown-emscripten)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(TOKENIZERS_CPP_CARGO_TARGET aarch64-apple-ios)
  # add extra dependency needed for rust tokenizer in iOS
  find_library(FOUNDATION_LIB Foundation)
  find_library(SECURITY_LIB Security)
  list(APPEND TOKENIZERS_C_LINK_LIBS ${FOUNDATION_LIB} ${SECURITY_LIB})
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(TOKENIZERS_CPP_CARGO_TARGET aarch64-apple-darwin)
  endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(TOKENIZERS_CPP_CARGO_TARGET aarch64-linux-android)
  set(CARGO_EXTRA_ENVS
    AR_aarch64_linux_android=${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-ar
    CC_aarch64_linux_android=${ANDROID_TOOLCHAIN_ROOT}/bin/aarch64-linux-android${ANDROID_NATIVE_API_LEVEL}-clang
    CXX_aarch64_linux_android=${ANDROID_TOOLCHAIN_ROOT}/bin/aarch64-linux-android${ANDROID_NATIVE_API_LEVEL}-clang++
  )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(TOKENIZERS_CPP_CARGO_TARGET x86_64-pc-windows-msvc)
endif()

if(WIN32)
  list(APPEND TOKENIZERS_C_LINK_LIBS
    ntdll wsock32 ws2_32 Bcrypt
    iphlpapi userenv psapi
  )
endif()

set(TOKENIZERS_CPP_CARGO_FLAGS "")
set(TOKENIZERS_CPP_CARGO_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TOKENIZERS_CPP_CARGO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

if (NOT TOKENIZERS_CPP_CARGO_TARGET STREQUAL "")
    list(APPEND TOKENIZERS_CPP_CARGO_FLAGS --target ${TOKENIZERS_CPP_CARGO_TARGET})
    set(TOKENIZERS_CPP_CARGO_BINARY_DIR
        "${TOKENIZERS_CPP_CARGO_BINARY_DIR}/${TOKENIZERS_CPP_CARGO_TARGET}")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(TOKENIZERS_CPP_CARGO_BINARY_DIR "${TOKENIZERS_CPP_CARGO_BINARY_DIR}/debug")
else ()
    list(APPEND TOKENIZERS_CPP_CARGO_FLAGS --release)
    set(TOKENIZERS_CPP_CARGO_BINARY_DIR "${TOKENIZERS_CPP_CARGO_BINARY_DIR}/release")
endif ()

get_filename_component(TOKENIZERS_CPP_ROOT ${CMAKE_CURRENT_LIST_FILE} DIRECTORY)
set(TOKENIZERS_CPP_CARGO_SOURCE_PATH ${TOKENIZERS_CPP_ROOT}/rust)


if(MSVC)
  set(TOKENIZERS_RUST_LIB "${TOKENIZERS_CPP_CARGO_BINARY_DIR}/tokenizers_c.lib")
else()
  set(TOKENIZERS_RUST_LIB "${TOKENIZERS_CPP_CARGO_BINARY_DIR}/libtokenizers_c.a")
endif()
set(TOKENIZERS_CPP_INCLUDE ${TOKENIZERS_CPP_ROOT}/include)

# NOTE: need to use cmake -E env to be portable in win
add_custom_command(
  OUTPUT ${TOKENIZERS_RUST_LIB}
  COMMAND
  ${CMAKE_COMMAND} -E env
  CARGO_TARGET_DIR=${TOKENIZERS_CPP_CARGO_TARGET_DIR}
  ${CARGO_EXTRA_ENVS}
  RUSTFLAGS="${TOKENIZERS_CPP_RUST_FLAGS}"
  cargo build ${TOKENIZERS_CPP_CARGO_FLAGS}
  WORKING_DIRECTORY ${TOKENIZERS_CPP_CARGO_SOURCE_PATH}
  POST_BUILD COMMAND
  ${CMAKE_COMMAND} -E copy
  ${TOKENIZERS_RUST_LIB} "${CMAKE_CURRENT_BINARY_DIR}"
)

set(
  TOKENIZER_CPP_SRCS
  src/huggingface_tokenizer.cc
)
add_library(tokenizer_cpp_objs OBJECT ${TOKENIZER_CPP_SRCS})
target_include_directories(tokenizer_cpp_objs PUBLIC ${TOKENIZERS_CPP_INCLUDE})
# std::make_unique requires C++14
set_target_properties(tokenizer_cpp_objs PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

# sentencepiece config
# option(SPM_ENABLE_SHARED "override sentence piece config" OFF)
# option(SPM_ENABLE_TCMALLOC "" OFF)
# provide macro if it does not exist in cmake system
# it is OK to skip those since we do not provide these apps in the ios
# instead just link to the sentencepiece directly
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
  macro (set_xcode_property TARGET XCODE_PROPERTY XCODE_VALUE)
      set_property (TARGET ${TARGET} PROPERTY
          XCODE_ATTRIBUTE_${XCODE_PROPERTY} ${XCODE_VALUE})
  endmacro (set_xcode_property)
endif()
# add_subdirectory(sentencepiece sentencepiece EXCLUDE_FROM_ALL)

add_library(tokenizers_c INTERFACE ${TOKENIZERS_RUST_LIB})
target_link_libraries(tokenizers_c INTERFACE ${TOKENIZERS_RUST_LIB} ${TOKENIZERS_C_LINK_LIBS} pthread)

add_library(tokenizers_cpp STATIC $<TARGET_OBJECTS:tokenizer_cpp_objs>)
target_link_libraries(tokenizers_cpp PUBLIC tokenizers_c)
target_include_directories(tokenizers_cpp PUBLIC ${TOKENIZERS_CPP_INCLUDE})
set_target_properties(tokenizers_cpp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)


# message("tokenizers: ${CMAKE_INSTALL_PREFIX}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libtokenizers_c.a DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(TARGETS tokenizers_cpp DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(DIRECTORY ${TOKENIZERS_CPP_INCLUDE}/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
    FILES_MATCHING PATTERN "*.h")
