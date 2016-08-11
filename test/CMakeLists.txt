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

target_link_libraries(rdb_test m)

install(TARGETS rdb_test DESTINATION test)

enable_testing()

add_test (rdb_test_init_clean rdb_test -t1)
set_tests_properties (rdb_test_init_clean
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok")

add_test (rdb_test_register_pools rdb_test -t2)
set_tests_properties (rdb_test_register_pools
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok\nOk")

add_test (rdb_test_data_16_0 rdb_test -t3 -l2 -i0)
set_tests_properties (rdb_test_data_16_0
    PROPERTIES PASS_REGULAR_EXPRESSION "^AAAA,AAAB,AABA,AABB,ABAA,ABAB,ABBA,ABBB,BAAA,BAAB,BABA,BABB,BBAA,BBAB,BBBA,BBBB,")

add_test (rdb_test_data_16_1 rdb_test -t3 -l2 -i1)
set_tests_properties (rdb_test_data_16_1
    PROPERTIES PASS_REGULAR_EXPRESSION "^aaaa,aaab,aaba,aabb,abaa,abab,abba,abbb,baaa,baab,baba,babb,bbaa,bbab,bbba,bbbb,")

add_test (rdb_test_data_16_2 rdb_test -t3 -l2 -i2)
set_tests_properties (rdb_test_data_16_2
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (rdb_test_data_16_3 rdb_test -t3 -l2 -i3)
set_tests_properties (rdb_test_data_16_3
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (rdb_test_data_16_4 rdb_test -t3 -l2 -i4)
set_tests_properties (rdb_test_data_16_4
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (rdb_test_data_16_5 rdb_test -t3 -l2 -i5)
set_tests_properties (rdb_test_data_16_5
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (rdb_test_data_16_6 rdb_test -t3 -l2 -i6)
set_tests_properties (rdb_test_data_16_6
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (rdb_test_data_16_7 rdb_test -t3 -l2 -i7)
set_tests_properties (rdb_test_data_16_7
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (rdb_test_data_16_8 rdb_test -t3 -l2 -i8)
set_tests_properties (rdb_test_data_16_8
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (rdb_test_data_16_9 rdb_test -t3 -l2 -i9)
set_tests_properties (rdb_test_data_16_9
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (rdb_test_data_16_10 rdb_test -t3 -l2 -i10)
set_tests_properties (rdb_test_data_16_10
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,")

add_test (rdb_test_data_16_11 rdb_test -t3 -l2 -i11)
set_tests_properties (rdb_test_data_16_11
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,")

add_test (rdb_test_data_get rdb_test -t4)
set_tests_properties (rdb_test_data_get
    PROPERTIES PASS_REGULAR_EXPRESSION "^DAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\n")
