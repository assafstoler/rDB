cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_BUILD_TYPE Debug)

add_library (cpp_helpers
    model_cpp_interface.cpp 
    )

target_include_directories (cpp_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories (cpp_helpers PRIVATE $<TARGET_PROPERTY:rdbfwlib,INCLUDE_DIRECTORIES>)

set_target_properties(cpp_helpers PROPERTIES PUBLIC_HEADER "model_cpp_interface.h;model_cpp.h")
INSTALL(TARGETS cpp_helpers
        PUBLIC_HEADER DESTINATION /usr/local/include/rdbfw
)
