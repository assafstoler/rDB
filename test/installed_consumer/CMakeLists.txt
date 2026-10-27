cmake_minimum_required(VERSION 3.1)
project(rdb_installed_consumer C CXX)
enable_testing()
find_package(Threads REQUIRED)

# Independent project: consume only the staged public headers and library.
foreach(language c cpp)
    set(consumer consumer-${language})
    add_executable(${consumer} ../rdb_installed_consumer.${language})
    set_target_properties(${consumer} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(${consumer} PRIVATE "${RDB_INCLUDE_DIR}")
    target_compile_options(${consumer} PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(${consumer} "${RDB_LIBRARY}" Threads::Threads)
    add_test(NAME ${consumer} COMMAND ${consumer})
endforeach()
