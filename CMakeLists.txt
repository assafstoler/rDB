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
add_subdirectory(tests)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -g -O0 -export-dynamic -avoid-version")
add_library(rdb_beta STATIC ${rdb_beta_SRCS})

enable_testing()

add_test (test_init_clean tests/rdb_test -t1)
set_tests_properties (test_init_clean
    PROPERTIES PASS_REGULAR_EXPRESSION "Ok")

add_test (test_register_pools tests/rdb_test -t2)
set_tests_properties (test_register_pools
    PROPERTIES PASS_REGULAR_EXPRESSION "Ok\nOk")

add_test (test_data_16_0 tests/rdb_test -t3 -l2 -i0)
set_tests_properties (test_data_16_0
    PROPERTIES PASS_REGULAR_EXPRESSION "AAAA\nAAAB\nAABA\nAABB\nABAA\nABAB\nABBA\nABBB\nBAAA\nBAAB\nBABA\nBABB\nBBAA\nBBAB\nBBBA\nBBBB")

add_test (test_data_16_1 tests/rdb_test -t3 -l2 -i1)
set_tests_properties (test_data_16_1
    PROPERTIES PASS_REGULAR_EXPRESSION "aaaa\naaab\naaba\naabb\nabaa\nabab\nabba\nabbb\nbaaa\nbaab\nbaba\nbabb\nbbaa\nbbab\nbbba\nbbbb")

########### install files ###############
#install(TARGETS rdb_beta DESTINATION /var/alertlogic/lib)
