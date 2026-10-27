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
add_executable(rdb_reindex_key_tests rdb_reindex_key_tests.c)
add_executable(rdb_api_tests rdb_api_tests.c)
add_executable(rdb_gc_tests rdb_gc_tests.c)
add_executable(rdb_stats_tests rdb_stats_tests.c)
add_executable(rdb_registry_lock_tests rdb_registry_lock_tests.c)
target_link_libraries(rdb_registry_lock_tests pthread)
add_executable(rdb_queue_delete_tests rdb_queue_delete_tests.c)
add_executable(rdb_pointer_key_tests rdb_pointer_key_tests.c)
add_executable(rdb_const_tests rdb_const_tests.c)
add_executable(rdb_const_matrix_tests rdb_const_matrix_tests.c)
add_executable(rdb_move_matrix_tests rdb_move_matrix_tests.c)
add_executable(rdb_model_tests rdb_model_tests.c)
add_executable(rdb_mixed_model_tests rdb_mixed_model_tests.c)
target_compile_options(rdb_mixed_model_tests PRIVATE -Wall -Wextra -Werror)
add_executable(rdb_string_neighbor_tests rdb_string_neighbor_tests.c)
add_executable(rdb_inline_string_tests rdb_inline_string_tests.c)
add_executable(rdb_string_cleanup_tests rdb_string_cleanup_tests.c)
add_executable(rdb_string_cleanup_matrix_tests rdb_string_cleanup_matrix_tests.c)
add_executable(rdb_iterate_delete_tests rdb_iterate_delete_tests.c)
add_executable(rdb_iterate_matrix_tests rdb_iterate_matrix_tests.c)
add_executable(rdb_iterate_companion_tests rdb_iterate_companion_tests.c)
target_compile_options(rdb_iterate_companion_tests PRIVATE -Wall -Wextra -Werror)
add_executable(rdb_dump_thread_tests rdb_dump_thread_tests.c)
target_link_libraries(rdb_dump_thread_tests pthread)
add_executable(rdb_dump_tests rdb_dump_tests.c)
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
    add_executable(rdb_shared_pool_thread_tests rdb_shared_pool_thread_tests.c)
    target_link_libraries(rdb_shared_pool_thread_tests pthread)

    add_executable(rdb_thread_contract_tests rdb_thread_contract_tests.c)
    target_link_libraries(rdb_thread_contract_tests pthread)
    target_compile_options(rdb_thread_contract_tests PRIVATE -Wall -Wextra -Werror)
    add_executable(rdb_stats_thread_tests rdb_stats_thread_tests.c)
    target_link_libraries(rdb_stats_thread_tests pthread)
    target_compile_options(rdb_stats_thread_tests PRIVATE -Wall -Wextra -Werror)

    add_executable(rdb_move_layout_tests rdb_move_layout_tests.c)
    target_link_libraries(rdb_move_layout_tests pthread)
    target_compile_options(rdb_move_layout_tests PRIVATE -Wall -Wextra -Werror)

    add_executable(rdb_error_tests rdb_error_tests.c
        "${CMAKE_SOURCE_DIR}/src/rdb.c")
    set_property(TARGET rdb_error_tests PROPERTY LINK_LIBRARIES pthread)
    set_property(TARGET rdb_error_tests APPEND_STRING PROPERTY LINK_FLAGS
        " -Wl,--wrap=malloc")

    add_executable(rdb_failure_injection_tests rdb_failure_injection_tests.c
        "${CMAKE_SOURCE_DIR}/src/rdb.c")
    # Override the directory-wide rdb linkage: this target compiles rdb.c.
    set_property(TARGET rdb_failure_injection_tests PROPERTY LINK_LIBRARIES pthread)
    set_property(TARGET rdb_failure_injection_tests APPEND_STRING PROPERTY LINK_FLAGS
        " -Wl,--wrap=malloc,--wrap=calloc,--wrap=snprintf,--wrap=__snprintf_chk")

    if(NOT CMAKE_CROSSCOMPILING)
        # Preserve compiler/sanitizer settings in a separate consumer build,
        # without inheriting the library's private definitions or include paths.
        set(rdb_consumer_flags "${CMAKE_CURRENT_BINARY_DIR}/installed-consumer-flags.cmake")
        file(WRITE "${rdb_consumer_flags}" "# Generated installed-consumer build settings.\n")
        set(rdb_consumer_variables CMAKE_C_COMPILER CMAKE_C_COMPILER_ARG1
            CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ARG1
            CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
        foreach(config ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER "${config}" config_upper)
            list(APPEND rdb_consumer_variables CMAKE_C_FLAGS_${config_upper}
                CMAKE_CXX_FLAGS_${config_upper} CMAKE_EXE_LINKER_FLAGS_${config_upper})
        endforeach()
        list(REMOVE_DUPLICATES rdb_consumer_variables)
        foreach(variable ${rdb_consumer_variables})
            file(APPEND "${rdb_consumer_flags}"
                "set(${variable} [==[${${variable}}]==] CACHE STRING \"\" FORCE)\n")
        endforeach()
        configure_file(check_installed_consumers.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/check_installed_consumers.cmake" @ONLY)
        add_test(NAME rdb_installed_consumers
            COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>" -P
                "${CMAKE_CURRENT_BINARY_DIR}/check_installed_consumers.cmake")
        set_tests_properties(rdb_installed_consumers PROPERTIES TIMEOUT 120)
    endif()
endif()

#install(TARGETS rdb_test DESTINATION test)

enable_testing()

add_test (NAME rdb_registration_guard_tests COMMAND rdb_registration_guard_tests)
add_test (NAME rdb_index_bounds_tests COMMAND rdb_index_bounds_tests)
add_test (NAME rdb_fault_tests COMMAND rdb_fault_tests)
add_test (NAME rdb_reindex_tests COMMAND rdb_reindex_tests)
add_test (NAME rdb_reindex_key_tests COMMAND rdb_reindex_key_tests)
add_test (NAME rdb_api_tests COMMAND rdb_api_tests)
add_test (NAME rdb_gc_tests COMMAND rdb_gc_tests)
add_test (NAME rdb_stats_tests COMMAND rdb_stats_tests)
add_test (NAME rdb_registry_lock_tests COMMAND rdb_registry_lock_tests)
add_test (NAME rdb_queue_delete_tests COMMAND rdb_queue_delete_tests)
add_test (NAME rdb_pointer_key_tests COMMAND rdb_pointer_key_tests)
add_test (NAME rdb_const_tests COMMAND rdb_const_tests)
add_test (NAME rdb_const_matrix_tests COMMAND rdb_const_matrix_tests)
add_test (NAME rdb_move_matrix_tests COMMAND rdb_move_matrix_tests)
add_test (NAME rdb_model_tests COMMAND rdb_model_tests)
foreach(case trees tree_fifo_lifo tree_lifo_fifo fifo_tree_lifo lifo_tree_fifo
        fifo_lifo_tree lifo_fifo_tree inline_strings pointer_strings)
    add_test(NAME rdb_mixed_model_${case} COMMAND rdb_mixed_model_tests ${case})
    set_tests_properties(rdb_mixed_model_${case} PROPERTIES
        TIMEOUT 30 LABELS "rdb;model")
endforeach()
add_test(NAME rdb_mixed_model_replay COMMAND rdb_mixed_model_tests
    stress --seed 0x17 --seeds 2 --items 8 --steps 200)
add_test(NAME rdb_mixed_model_options COMMAND "${CMAKE_COMMAND}"
    "-DMODEL=$<TARGET_FILE:rdb_mixed_model_tests>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_model_options.cmake")
set_tests_properties(rdb_mixed_model_replay rdb_mixed_model_options PROPERTIES
    TIMEOUT 30 LABELS "rdb;model")
# Longer correctness sequences are opt-in and have no timing assertions.
add_custom_target(model_stress
    COMMAND $<TARGET_FILE:rdb_mixed_model_tests> stress
        --seed 0x243f6a88 --seeds 8 --items 128 --steps 5000
    DEPENDS rdb_mixed_model_tests USES_TERMINAL)
add_test (NAME rdb_string_neighbor_tests COMMAND rdb_string_neighbor_tests)
add_test (NAME rdb_inline_string_tests COMMAND rdb_inline_string_tests)
add_test (NAME rdb_string_cleanup_tests COMMAND rdb_string_cleanup_tests)
add_test (NAME rdb_string_cleanup_matrix_tests COMMAND rdb_string_cleanup_matrix_tests)
add_test (NAME rdb_iterate_delete_tests COMMAND rdb_iterate_delete_tests)
add_test (NAME rdb_iterate_matrix_tests COMMAND rdb_iterate_matrix_tests)
foreach(case numeric strings fifo lifo aborts collisions)
    add_test(NAME rdb_iterate_companion_${case} COMMAND rdb_iterate_companion_tests ${case})
    set_tests_properties(rdb_iterate_companion_${case} PROPERTIES
        TIMEOUT 30 LABELS "rdb;iterate_companion")
endforeach()
add_test (NAME rdb_dump_thread_tests COMMAND rdb_dump_thread_tests)
add_test (NAME rdb_dump_tests COMMAND rdb_dump_tests)
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
if(TARGET rdb_shared_pool_thread_tests)
    add_test (NAME rdb_shared_pool_thread_tests COMMAND rdb_shared_pool_thread_tests)
endif()
if(TARGET rdb_thread_contract_tests)
    foreach(case registry registry_lookup registry_lookup_empty duplicate_name shared_pool)
        add_test(NAME rdb_thread_contract_${case} COMMAND rdb_thread_contract_tests ${case})
        set_tests_properties(rdb_thread_contract_${case} PROPERTIES
            TIMEOUT 30 LABELS "rdb;concurrency")
    endforeach()
endif()
if(TARGET rdb_stats_thread_tests)
    foreach(case mutations registry locked_pool)
        add_test(NAME rdb_stats_thread_${case} COMMAND rdb_stats_thread_tests ${case})
        set_tests_properties(rdb_stats_thread_${case} PROPERTIES
            TIMEOUT 30 LABELS "rdb;concurrency")
    endforeach()
endif()
if(TARGET rdb_move_layout_tests)
    foreach(case matrix threaded)
        add_test(NAME rdb_move_layout_${case} COMMAND rdb_move_layout_tests ${case})
        set_tests_properties(rdb_move_layout_${case} PROPERTIES
            TIMEOUT 30 LABELS "rdb;move")
    endforeach()
endif()
if(TARGET rdb_error_tests)
    add_test (NAME rdb_error_tests COMMAND rdb_error_tests)
endif()
if(TARGET rdb_failure_injection_tests)
    add_test (NAME rdb_failure_injection_tests COMMAND rdb_failure_injection_tests)
endif()

find_program(RDB_TEST_PYTHON NAMES python3)
if(RDB_TEST_PYTHON)
    add_test(NAME coverage_runner_tests COMMAND "${RDB_TEST_PYTHON}" -B
        "${CMAKE_SOURCE_DIR}/tests/test_coverage_runner.py")
    set_tests_properties(coverage_runner_tests PROPERTIES TIMEOUT 120)
    if(TEAL_COVERAGE_GCOVR)
        set_tests_properties(coverage_runner_tests PROPERTIES
            ENVIRONMENT "TEAL_TEST_GCOVR=${TEAL_COVERAGE_GCOVR}")
    endif()
endif()

# Require successful execution as well as the historical stdout expectations.
function(rdb_legacy_case name expected)
    add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}"
        "-DBINARY=$<TARGET_FILE:rdb_test>" "-DARGUMENTS=${ARGN}"
        "-DEXPECTED=${expected}" -P "${CMAKE_CURRENT_SOURCE_DIR}/check_legacy.cmake")
    set_tests_properties(${name} PROPERTIES TIMEOUT 30 LABELS "rdb;legacy")
endfunction()

rdb_legacy_case(rdb_test_init_clean "^Ok\n$" -t1)

rdb_legacy_case(rdb_test_register_pools "^Ok\nOk\n$" -t2)

rdb_legacy_case(rdb_test_data_16_0 "^AAAA,AAAB,AABA,AABB,ABAA,ABAB,ABBA,ABBB,BAAA,BAAB,BABA,BABB,BBAA,BBAB,BBBA,BBBB,\n$" -t3 -l2 -i0)

rdb_legacy_case(rdb_test_data_16_1 "^aaaa,aaab,aaba,aabb,abaa,abab,abba,abbb,baaa,baab,baba,babb,bbaa,bbab,bbba,bbbb,\n$" -t3 -l2 -i1)

rdb_legacy_case(rdb_test_data_16_2 "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$" -t3 -l2 -i2)

rdb_legacy_case(rdb_test_data_16_3 "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$" -t3 -l2 -i3)

rdb_legacy_case(rdb_test_data_16_4 "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$" -t3 -l2 -i4)

rdb_legacy_case(rdb_test_data_16_5 "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$" -t3 -l2 -i5)

rdb_legacy_case(rdb_test_data_16_6 "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$" -t3 -l2 -i6)

rdb_legacy_case(rdb_test_data_16_7 "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$" -t3 -l2 -i7)

rdb_legacy_case(rdb_test_data_16_8 "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$" -t3 -l2 -i8)

rdb_legacy_case(rdb_test_data_16_9 "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$" -t3 -l2 -i9)

rdb_legacy_case(rdb_test_data_16_10 "^0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n$" -t3 -l2 -i10)

rdb_legacy_case(rdb_test_data_16_11 "^-15,-14,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0,\n$" -t3 -l2 -i11)

rdb_legacy_case(rdb_test_data_16_13 "^AAAA,BAAA,ABAA,BBAA,AABA,BABA,ABBA,BBBA,AAAB,BAAB,ABAB,BBAB,AABB,BABB,ABBB,BBBB,\n$" -t3 -l2 -i14)

rdb_legacy_case(rdb_test_data_16_14 "^BBBB,ABBB,BABB,AABB,BBAB,ABAB,BAAB,AAAB,BBBA,ABBA,BABA,AABA,BBAA,ABAA,BAAA,AAAA,\n$" -t3 -l2 -i15)

rdb_legacy_case(rdb_test_data_get "^DAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\nDAAA\n$" -t4)

rdb_legacy_case(rdb_test_data_move "^0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15,\n0,8,4,12,10,6,14,1,9,5,13,3,11,7,15,\n0,8,4,12,10,6,14,1,9,5,\n0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\n14,13,12,11,10,9,8,7,6,5,4,3,2,1,\n14,13,12,11,10,9,8,7,6,5,4,3,1,\n1,3,4,5,6,7,8,9,10,11,12,13,14,\n1,3,4,5,6,7,8,9,10,11,12,13,14,\ninsert return = 17\ndump pool2 :8,\nre-insert return = 0\ndump pool2 :\ndump pool2 :8,\nGet OK\nNULL Get OK\nNeigh Get - hit OK\nNeigh Get - miss OK\nsearch 10 b 9 a 11\nNeigh Get - miss OK\nsearch 10 b 9 a 11\n1,3,5,6,7,9,11,13,14,\n4.8.10.\nMatch 5\nBetween test Before/After 5/6\nNull test Before/After -/1\n\nlock 0\nunlock\n$" -t5)

rdb_legacy_case(rdb_test_registra "^rDB: Fatal: Duplicte pool name in rdb_register_pool\nPool 'null_custom_pool' index 0 flags .*: RDB_KCF requires a comparator\nIndex 0 \\\(zero\\\) can only be set via rdb_register_pool\nIndex outside pool's declared index range\nRedefinition of used index not allowed\nIndex outside pool's declared index range\nOk\n$" -t6)

add_executable(rdb_legacy_probe legacy_probe.c)
# The subprocess probe exercises the runner and has no library dependency.
set_property(TARGET rdb_legacy_probe PROPERTY LINK_LIBRARIES "")
add_test(NAME rdb_legacy_runner COMMAND "${CMAKE_COMMAND}"
    "-DPROBE=$<TARGET_FILE:rdb_legacy_probe>"
    "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/check_legacy.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_legacy_runner.cmake")
set_tests_properties(rdb_legacy_runner PROPERTIES TIMEOUT 30 LABELS "rdb;legacy")
