cmake_minimum_required (VERSION 2.8)
project (rDB)

SET(rdb_beta_SRCS
   src/rDB.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g")

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories(
)

add_subdirectory(demo)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g -O0 -export-dynamic -avoid-version")
add_library(rdb_beta STATIC ${rdb_beta_SRCS})

########### install files ###############
#install(TARGETS rdb_beta DESTINATION /var/alertlogic/lib)
