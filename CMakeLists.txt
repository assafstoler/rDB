cmake_minimum_required (VERSION 2.8)
project (rDB)

SET(rdb_beta_SRCS
   src/rDB.c
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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -O2 -g -fms-extensions")

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

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g -O0 -export-dynamic")
add_library(rdb_beta STATIC ${rdb_beta_SRCS})

add_subdirectory(demo)
add_subdirectory(rdbfw)
add_subdirectory(test)
add_subdirectory(kernel)


enable_testing()

