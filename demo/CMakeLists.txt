project (rDBdemo)

SET(rdb_demo_SRCS
   rDBDemo.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g")

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories ("${PROJECT_SOURCE_DIR}/")

link_libraries (rdb_beta)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
add_executable(rDBDemo rDBDemo.c)


########### install files ###############
#install(TARGETS rdb_beta DESTINATION /var/alertlogic/lib)
