cmake_minimum_required (VERSION 2.8)
project (rDB)

SET(rdb_SRCS
   src/rdb.c
)
SET(rdb_INC
   src/rdb.h
)
macro(use_c99)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "-std=gnu99 ${CMAKE_C_FLAGS}")
    endif ()
  else ()
    set (CMAKE_C_STANDARD 99)
  endif ()
endmacro(use_c99)

macro(use_c11)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "-std=gnu11 ${CMAKE_C_FLAGS}")
    endif ()
  else ()
    set (CMAKE_C_STANDARD 11)
    set (CMAKE_CXX_STANDARD 11)
  endif ()
endmacro(use_c11)

macro(use_c14)
  if (CMAKE_VERSION VERSION_LESS "3.1")
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
      set (CMAKE_C_FLAGS "-std=gnu14 ${CMAKE_C_FLAGS}")
    endif ()
  else ()
    set (CMAKE_C_STANDARD 11)
    set (CMAKE_CXX_STANDARD 14)
  endif ()
endmacro(use_c14)

use_c14()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fms-extensions")
set(CMAKE_C_FLAGS_DEBUG "-Wall -O0 -g")
set(CMAKE_C_FLAGS_RELEASE "-Wall -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fPIC -fms-extensions")
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-Wall -O3")

if(NOT DEFINED "SKIP_KERNEL")
    set(SKIP_KERNEL "false")
endif()


include_directories("${PROJECT_SOURCE_DIR}/src")

link_directories(
)


execute_process(
    COMMAND git describe --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

configure_file (
    "${PROJECT_SOURCE_DIR}/version.h.in"
    "${PROJECT_BINARY_DIR}/version.h"
)

INCLUDE(CheckTypeSize)
CHECK_TYPE_SIZE(__uint128_t USE_128_BIT_TYPES)
if (${USE_128_BIT_TYPES})
 ADD_DEFINITIONS(-DUSE_128_BIT_TYPES)
 MESSAGE("Using native 128 bit types")
else  (${USE_128_BIT_TYPES})
 MESSAGE("NOT Using native 128 bit types")
endif (${USE_128_BIT_TYPES})

ADD_DEFINITIONS(-DRDB_POOL_COUNTERS)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g -export-dynamic")
add_library(rdb SHARED ${rdb_SRCS} ${rdb_INC})

set(RDBLIB_VERSION_MAJOR 1)
set(RDBLIB_VERSION_MINOR 0)
set(RDBLIB_VERSION_PATCH 0)
set(RDBLIB_VERSION_STRING ${RDBLIB_VERSION_MAJOR}.${RDBLIB_VERSION_MINOR}.${RDBLIB_VERSION_PATCH})

if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    set(LIBSUFFIX 64)
else()
    set(LIBSUFFIX "")
    #set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}64)
endif()
set(INSTALL_LIB_DIR     lib${LIBSUFFIX} CACHE PATH "Installation directory for libraries")
mark_as_advanced(INSTALL_LIB_DIR)


INCLUDE_DIRECTORIES(${CMAKE_SRC_DIR}/src)
set_target_properties(rdb PROPERTIES PUBLIC_HEADER ${rdb_INC})
set_target_properties(rdb PROPERTIES VERSION ${RDBLIB_VERSION_STRING} SOVERSION ${RDBLIB_VERSION_MAJOR} )
INSTALL(TARGETS rdb
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${INSTALL_LIB_DIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/${INSTALL_LIB_DIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_PREFIX}/include/rdb
)

message ("libdir==${CMAKE_INSTALL_LIBDIR}")
message ("libdir==${CMAKE_INSTALL_PREFIX}")


add_subdirectory(demo)
#add_subdirectory(rdbfw)
add_subdirectory(test)
if (NOT ${SKIP_KERNEL})
    add_subdirectory(kernel)
endif()

enable_testing()

