project (rdb_test)

SET(rdb_test_SRCS
   rdb_test.c
)

include_directories ("${PROJECT_SOURCE_DIR}/src")

link_directories ("${PROJECT_SOURCE_DIR}/")

link_libraries (rdb)

#set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -O0 -export-dynamic -avoid-version -g")
 
add_executable(rdb_test rdb_test.c)

target_link_libraries(rdb_test m)

add_executable(rdb_registration_guard_tests rdb_registration_guard_tests.c)
add_executable(rdb_index_bounds_tests rdb_index_bounds_tests.c)
add_executable(rdb_fault_tests rdb_fault_tests.c)
add_executable(rdb_reindex_tests rdb_reindex_tests.c)
add_executable(rdb_gc_tests rdb_gc_tests.c)
add_executable(rdb_stats_tests rdb_stats_tests.c)
add_executable(rdb_registry_lock_tests rdb_registry_lock_tests.c)
target_link_libraries(rdb_registry_lock_tests pthread)
add_executable(rdb_queue_delete_tests rdb_queue_delete_tests.c)
add_executable(rdb_header_consumer_c rdb_header_consumer.c)
add_executable(rdb_header_consumer_cpp rdb_header_consumer.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(rdb_mutex_init_tests rdb_mutex_init_tests.c
        "${CMAKE_SOURCE_DIR}/src/rdb.c")
    set_property(TARGET rdb_mutex_init_tests PROPERTY LINK_LIBRARIES pthread)
    set_property(TARGET rdb_mutex_init_tests APPEND_STRING PROPERTY LINK_FLAGS
        " -Wl,--wrap=pthread_mutex_init,--wrap=pthread_mutex_destroy")

    add_executable(rdb_stats_no_counters_tests rdb_stats_tests.c
        "${CMAKE_SOURCE_DIR}/src/rdb.c")
    set_property(TARGET rdb_stats_no_counters_tests PROPERTY LINK_LIBRARIES pthread)
    set_property(TARGET rdb_stats_no_counters_tests APPEND_STRING PROPERTY COMPILE_FLAGS
        " -URDB_POOL_COUNTERS")

    add_executable(rdb_lock_debug_tests rdb_lock_debug_tests.c
        "${CMAKE_SOURCE_DIR}/src/rdb.c")
    set_property(TARGET rdb_lock_debug_tests PROPERTY LINK_LIBRARIES pthread)
    set_property(TARGET rdb_lock_debug_tests APPEND_STRING PROPERTY COMPILE_FLAGS
        " -DRDB_LOCK_DEBUG")

    add_executable(rdb_stats_concurrency_tests rdb_stats_concurrency_tests.c)
    target_link_libraries(rdb_stats_concurrency_tests pthread)
endif()

#install(TARGETS rdb_test DESTINATION test)

enable_testing()

add_test (NAME rdb_registration_guard_tests COMMAND rdb_registration_guard_tests)
add_test (NAME rdb_index_bounds_tests COMMAND rdb_index_bounds_tests)
add_test (NAME rdb_fault_tests COMMAND rdb_fault_tests)
add_test (NAME rdb_reindex_tests COMMAND rdb_reindex_tests)
add_test (NAME rdb_gc_tests COMMAND rdb_gc_tests)
add_test (NAME rdb_stats_tests COMMAND rdb_stats_tests)
add_test (NAME rdb_registry_lock_tests COMMAND rdb_registry_lock_tests)
add_test (NAME rdb_queue_delete_tests COMMAND rdb_queue_delete_tests)
add_test (NAME rdb_header_consumer_c COMMAND rdb_header_consumer_c)
add_test (NAME rdb_header_consumer_cpp COMMAND rdb_header_consumer_cpp)
if(TARGET rdb_mutex_init_tests)
    add_test (NAME rdb_mutex_init_tests COMMAND rdb_mutex_init_tests)
endif()
if(TARGET rdb_stats_no_counters_tests)
    add_test (NAME rdb_stats_no_counters_tests COMMAND rdb_stats_no_counters_tests)
endif()
if(TARGET rdb_lock_debug_tests)
    add_test (NAME rdb_lock_debug_tests COMMAND rdb_lock_debug_tests)
endif()
if(TARGET rdb_stats_concurrency_tests)
    add_test (NAME rdb_stats_concurrency_tests COMMAND rdb_stats_concurrency_tests)
endif()

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
    PROPERTIES PASS_REGULAR_EXPRESSION "^rDB: Fatal: Duplicte pool name in rdb_register_pool\nPool 'null_custom_pool' index 0 flags .*: RDB_KCF requires a comparator\nIndex 0 \\\(zero\\\) can only be set via rdb_register_pool\nIndex outside pool's declared index range\nRedefinition of used index not allowed\nIndex outside pool's declared index range\nOk\n$")

