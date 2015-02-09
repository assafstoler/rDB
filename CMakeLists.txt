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
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok")

add_test (test_register_pools tests/rdb_test -t2)
set_tests_properties (test_register_pools
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok\nOk")

add_test (test_data_16_0 tests/rdb_test -t3 -l2 -i0)
set_tests_properties (test_data_16_0
    PROPERTIES PASS_REGULAR_EXPRESSION "^AAAA,AAAB,AABA,AABB,ABAA,ABAB,ABBA,ABBB,BAAA,BAAB,BABA,BABB,BBAA,BBAB,BBBA,BBBB,")

add_test (test_data_16_1 tests/rdb_test -t3 -l2 -i1)
set_tests_properties (test_data_16_1
    PROPERTIES PASS_REGULAR_EXPRESSION "^aaaa,aaab,aaba,aabb,abaa,abab,abba,abbb,baaa,baab,baba,babb,bbaa,bbab,bbba,bbbb,")

add_test (test_data_16_2 tests/rdb_test -t3 -l2 -i2)
set_tests_properties (test_data_16_2
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (test_data_16_3 tests/rdb_test -t3 -l2 -i3)
set_tests_properties (test_data_16_3
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (test_data_16_4 tests/rdb_test -t3 -l2 -i4)
set_tests_properties (test_data_16_4
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (test_data_16_5 tests/rdb_test -t3 -l2 -i5)
set_tests_properties (test_data_16_5
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (test_data_16_6 tests/rdb_test -t3 -l2 -i6)
set_tests_properties (test_data_16_6
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (test_data_16_7 tests/rdb_test -t3 -l2 -i7)
set_tests_properties (test_data_16_7
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (test_data_16_8 tests/rdb_test -t3 -l2 -i8)
set_tests_properties (test_data_16_8
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (test_data_16_9 tests/rdb_test -t3 -l2 -i9)
set_tests_properties (test_data_16_9
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (test_data_16_10 tests/rdb_test -t3 -l2 -i10)
set_tests_properties (test_data_16_10
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (test_data_16_11 tests/rdb_test -t3 -l2 -i11)
set_tests_properties (test_data_16_11
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (test_data_get tests/rdb_test -t4)
set_tests_properties (test_data_get
    PROPERTIES PASS_REGULAR_EXPRESSION "^DAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\n")

########### install files ###############
#install(TARGETS rdb_beta DESTINATION /usr/loca/bin)
