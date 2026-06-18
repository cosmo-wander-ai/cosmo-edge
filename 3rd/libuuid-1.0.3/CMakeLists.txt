cmake_minimum_required(VERSION 3.10)

project(uuid LANGUAGES C)

# 选项
option(BUILD_SHARED "Build shared library" ON)

# 检查系统功能
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckLibraryExists)

check_function_exists(usleep HAVE_USLEEP)
check_function_exists(nanosleep HAVE_NANOSLEEP)
check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)
check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
check_function_exists(reallocarray HAVE_REALLOCARRAY)
check_function_exists(random HAVE_RANDOM)
check_function_exists(srandom HAVE_SRANDOM)
check_function_exists(rand_r HAVE_RAND_R)

check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(sys/file.h HAVE_SYS_FILE_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(time.h HAVE_TIME_H)
check_include_file(stdint.h HAVE_STDINT_H)

if(HAVE_USLEEP)
    add_compile_definitions(HAVE_USLEEP=1)
endif()

if(HAVE_NANOSLEEP)
    add_compile_definitions(HAVE_NANOSLEEP=1)
endif()

if(HAVE_GETTIMEOFDAY)
    add_compile_definitions(HAVE_GETTIMEOFDAY=1)
endif()

if(HAVE_CLOCK_GETTIME)
    add_compile_definitions(HAVE_CLOCK_GETTIME=1)
endif()

if(HAVE_REALLOCARRAY AND UUID_USE_SYSTEM)
    add_compile_definitions(HAVE_REALLOCARRAY=1)
else()
    add_compile_definitions(HAVE_REALLOCARRAY=0)
endif()

if(HAVE_RANDOM)
    add_compile_definitions(HAVE_RANDOM=1)
endif()

if(HAVE_SRANDOM)
    add_compile_definitions(HAVE_SRANDOM=1)
endif()

if(HAVE_RAND_R)
    add_compile_definitions(HAVE_RAND_R=1)
endif()

if(HAVE_SYS_TIME_H)
    add_compile_definitions(HAVE_SYS_TIME_H=1)
endif()

if(HAVE_SYS_FILE_H)
    add_definitions(-DHAVE_SYS_FILE_H)
endif()

if(HAVE_UNISTD_H)
    add_compile_definitions(HAVE_UNISTD_H=1)
endif()

if(HAVE_TIME_H)
    add_compile_definitions(HAVE_TIME_H=1)
endif()

set(UUID_HEADERS uuid.h)

set(UUID_SOURCES
    clear.c
    compare.c
    copy.c
    gen_uuid.c
    isnull.c
    pack.c
    parse.c
    randutils.c
    unpack.c
    unparse.c
    uuid_time.c
)


if(BUILD_SHARED)
    add_library(uuid SHARED ${UUID_SOURCES})
else()
    add_library(uuid STATIC ${UUID_SOURCES})
endif()

target_compile_definitions(uuid PRIVATE 
    _DEFAULT_SOURCE
    _GNU_SOURCE
)

install(TARGETS uuid DESTINATION lib)
install(FILES ${UUID_HEADERS} DESTINATION include)