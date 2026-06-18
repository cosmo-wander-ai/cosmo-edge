# set cross-compiled system type, it's better not use the type which cmake cannot recognized.
SET ( CMAKE_SYSTEM_NAME Linux )
SET ( CMAKE_SYSTEM_PROCESSOR aarch64 )

SET ( CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc CACHE PATH "C compiler path" FORCE)
SET ( CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++ CACHE PATH "C++ compiler path" FORCE)

SET( CMAKE_AR /usr/bin/aarch64-linux-gnu-ar CACHE PATH "ar path" FORCE)
SET( CMAKE_STRIP /usr/bin/aarch64-linux-gnu-strip CACHE PATH "strip path" FORCE)
SET( CMAKE_RANLIB /usr/bin/aarch64-linux-gnu-ranlib CACHE PATH "ranlib path" FORCE)


# set searching rules for cross-compiler
SET ( CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER )
SET ( CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY )
SET ( CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY )

# set ${CMAKE_C_FLAGS} and ${CMAKE_CXX_FLAGS}flag for cross-compiled process
# SET ( CMAKE_CXX_FLAGS "-std=c++11 -fPIC ${CMAKE_CXX_FLAGS}" )

# other settings
add_definitions(-D__ARM_NEON)

# add_compile_options(-std=c++17)
add_compile_options(-fPIC)