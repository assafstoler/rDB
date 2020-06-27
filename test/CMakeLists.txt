project (rdb_test)

SET(rdb_test_SRCS
   rdb_test.c
)

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories ("${PROJECT_SOURCE_DIR}/")

link_libraries (rdb_beta)

#set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
add_executable(rdb_test rdb_test.c)

target_link_libraries(rdb_test m)

#install(TARGETS rdb_test DESTINATION test)

enable_testing()

add_test (rdb_test_init_clean rdb_test -t1)
set_tests_properties (rdb_test_init_clean
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok\n$")

add_test (rdb_test_register_pools rdb_test -t2)
set_tests_properties (rdb_test_register_pools
    PROPERTIES PASS_REGULAR_EXPRESSION "^Ok\nOk\n$")

add_test (rdb_test_data_16_0 rdb_test -t3 -l2 -i0)
set_tests_properties (rdb_test_data_16_0
    PROPERTIES PASS_REGULAR_EXPRESSION "^AAAA,AAAB,AABA,AABB,ABAA,ABAB,ABBA,ABBB,BAAA,BAAB,BABA,BABB,BBAA,BBAB,BBBA,BBBB,\n$")

add_test (rdb_test_data_16_1 rdb_test -t3 -l2 -i1)
set_tests_properties (rdb_test_data_16_1
    PROPERTIES PASS_REGULAR_EXPRESSION "^aaaa,aaab,aaba,aabb,abaa,abab,abba,abbb,baaa,baab,baba,babb,bbaa,bbab,bbba,bbbb,\n$")

add_test (rdb_test_data_16_2 rdb_test -t3 -l2 -i2)
set_tests_properties (rdb_test_data_16_2
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$")

add_test (rdb_test_data_16_3 rdb_test -t3 -l2 -i3)
set_tests_properties (rdb_test_data_16_3
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$")

add_test (rdb_test_data_16_4 rdb_test -t3 -l2 -i4)
set_tests_properties (rdb_test_data_16_4
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$")

add_test (rdb_test_data_16_5 rdb_test -t3 -l2 -i5)
set_tests_properties (rdb_test_data_16_5
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$")

add_test (rdb_test_data_16_6 rdb_test -t3 -l2 -i6)
set_tests_properties (rdb_test_data_16_6
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$")

add_test (rdb_test_data_16_7 rdb_test -t3 -l2 -i7)
set_tests_properties (rdb_test_data_16_7
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$")

add_test (rdb_test_data_16_8 rdb_test -t3 -l2 -i8)
set_tests_properties (rdb_test_data_16_8
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$")

add_test (rdb_test_data_16_9 rdb_test -t3 -l2 -i9)
set_tests_properties (rdb_test_data_16_9
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$")

add_test (rdb_test_data_16_10 rdb_test -t3 -l2 -i10)
set_tests_properties (rdb_test_data_16_10
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$")

add_test (rdb_test_data_16_11 rdb_test -t3 -l2 -i11)
set_tests_properties (rdb_test_data_16_11
    PROPERTIES PASS_REGULAR_EXPRESSION "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$")

add_test (rdb_test_data_16_13 rdb_test -t3 -l2 -i14)
set_tests_properties (rdb_test_data_16_13
    PROPERTIES PASS_REGULAR_EXPRESSION "^AAAA,BAAA,ABAA,BBAA,AABA,BABA,ABBA,BBBA,AAAB,BAAB,ABAB,BBAB,AABB,BABB,ABBB,BBBB,\n$")

add_test (rdb_test_data_16_14 rdb_test -t3 -l2 -i15)
set_tests_properties (rdb_test_data_16_14
    PROPERTIES PASS_REGULAR_EXPRESSION "^BBBB,ABBB,BABB,AABB,BBAB,ABAB,BAAB,AAAB,BBBA,ABBA,BABA,AABA,BBAA,ABAA,BAAA,AAAA,\n$")

add_test (rdb_test_data_get rdb_test -t4)
set_tests_properties (rdb_test_data_get
    PROPERTIES PASS_REGULAR_EXPRESSION "^DAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\n$")

add_test (rdb_test_data_move rdb_test -t5)
set_tests_properties (rdb_test_data_move
    PROPERTIES PASS_REGULAR_EXPRESSION "^0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15,\n0,8,4,12,10,6,14,1,9,5,13,3,11,7,15,\n0,8,4,12,10,6,14,1,9,5,\n0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n14,13,12,11,10,9,8,7,6,5,4,3,2,1,\n14,13,12,11,10,9,8,7,6,5,4,3,1,\n1,3,4,5,6,7,8,9,10,11,12,13,14,\n1,3,4,5,6,7,8,9,10,11,12,13,14,\ninsert return = 17\ndump pool2 :8,\nre-insert return = 0\ndump pool2 :\ndump pool2 :8,\nGet OK\nNULL Get OK\nNeigh Get - hit OK\nNeigh Get - miss OK\nsearch 10 b 9 a 11\nNeigh Get - miss OK\nsearch 10 b 9 a 11\n1,3,5,6,7,9,11,13,14,\n4.8.10.\nMatch 5\nBetween test Before/After 5/6\nNull test Before/After -/1\n\nlock 0\nunlock\n$")

add_test (rdb_test_registra rdb_test -t6)
set_tests_properties (rdb_test_registra
    PROPERTIES PASS_REGULAR_EXPRESSION "^rDB: Fatal: Duplicte pool name in rdb_register_pool\nrDB: Fatal: pool registration without type or matching compare fn\nIndex 0 \\\(zero\\\) can only be set via rdb_register_pool\nIndex >= RDB_POOL_MAX_IDX\nRedefinition of used index not allowed\nIndex Registration without valid type or compare fn. Ignored\nOk\n$")

