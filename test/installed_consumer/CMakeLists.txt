cmake_minimum_required(VERSION 3.1)
project(rdb_installed_consumer C CXX)
enable_testing()
find_package(Threads REQUIRED)

# Independent project: consume only staged public headers and library variants.
foreach(language c cpp)
    foreach(library normal no_counters)
        foreach(counters undefined defined)
            set(consumer consumer_${language}_${library}_${counters})
            add_executable(${consumer} ../rdb_installed_consumer.${language})
            set_target_properties(${consumer} PROPERTIES
                C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
            target_include_directories(${consumer} PRIVATE "${RDB_INCLUDE_DIR}")
            target_compile_options(${consumer} PRIVATE -Wall -Wextra -Werror
                -UUSE_128_BIT_TYPES -URDB_POOL_COUNTERS)
            if(counters STREQUAL "defined")
                target_compile_options(${consumer} PRIVATE -DRDB_POOL_COUNTERS)
            endif()
            target_compile_definitions(${consumer} PRIVATE
                RDB_TEST_VALUE_BYTES=${RDB_VALUE_BYTES})
            if(library STREQUAL "normal")
                target_compile_definitions(${consumer} PRIVATE RDB_TEST_LIBRARY_COUNTERS=1)
            else()
                target_compile_definitions(${consumer} PRIVATE RDB_TEST_LIBRARY_COUNTERS=0)
            endif()
            target_link_libraries(${consumer} "${RDB_LIBRARY_${library}}" Threads::Threads)
            add_test(NAME ${consumer} COMMAND ${consumer})
            set_tests_properties(${consumer} PROPERTIES TIMEOUT 30)
        endforeach()
    endforeach()
endforeach()
