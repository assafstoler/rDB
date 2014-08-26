project (rdb_test)

SET(rdb_test_SRCS
   rdb_test.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fPIC -g -lm")

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories ("${PROJECT_SOURCE_DIR}/")

link_libraries (rdb_beta)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
add_executable(rdb_test rdb_test.c)




########### install files ###############
#install(TARGETS rdb_beta DESTINATION /var/alertlogic/lib)
