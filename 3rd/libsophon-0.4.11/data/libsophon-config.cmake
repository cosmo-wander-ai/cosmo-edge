set(LIBSOPHON_INCLUDE_DIRS "/opt/sophon/libsophon-current/include")
set(LIBSOPHON_LIB_DIRS "/opt/sophon/libsophon-current/lib")

file(GLOB lib_names RELATIVE ${LIBSOPHON_LIB_DIRS} ${LIBSOPHON_LIB_DIRS}/*)

foreach(name ${lib_names})
    find_library(the_${name} "${name}" PATHS ${LIBSOPHON_LIB_DIRS})
endforeach()

