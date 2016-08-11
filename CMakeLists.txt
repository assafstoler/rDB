cmake_minimum_required (VERSION 2.8)
project (rDB)

SET(rdb_beta_SRCS
   src/rDB.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g")

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories(
)

INCLUDE(CheckTypeSize)

CHECK_TYPE_SIZE(__uint128_t USE_128_BIT_TYPES)
if (${USE_128_BIT_TYPES})
 ADD_DEFINITIONS(-DUSE_128_BIT_TYPES)
 MESSAGE("Using native 128 bit types")
else  (${USE_128_BIT_TYPES})
 MESSAGE("NOT Using native 128 bit types")
endif (${USE_128_BIT_TYPES})

add_subdirectory(demo)
add_subdirectory(rdbfw)
add_subdirectory(test)
add_subdirectory(kernel)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g -O2 -export-dynamic -avoid-version")
add_library(rdb_beta STATIC ${rdb_beta_SRCS})

enable_testing()

