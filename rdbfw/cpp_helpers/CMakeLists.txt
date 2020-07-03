cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_BUILD_TYPE Debug)

# Set our project and our required packages
#project( rdb_mavlink )
#find_package( Threads REQUIRED )

use_c14()

#ADD_LIBRARY(rdb_mavlink
#    SHARED
#    rdb_mavlink.c)
#target_link_libraries(rdb_mavlink pthread dl rdblib rt rdbfwlib)


add_library (cpp_helpers
    model_cpp_interface.cpp 
    )

target_include_directories (cpp_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories (cpp_helpers PRIVATE $<TARGET_PROPERTY:rdbfwlib,INCLUDE_DIRECTORIES>)

#target_link_libraries (cpp_helpers PRIVATE
    #/camera_mdl
    #    port_io_udp
    #    )
    #target_include_directories (cpp_helpers PRIVATE $<TARGET_PROPERTY:port_io_udp,INCLUDE_DIRECTORIES>)
    #target_include_directories (cpp_helpers PRIVATE $<TARGET_PROPERTY:camera_mdl,INCLUDE_DIRECTORIES>)
    #target_include_directories (cpp_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#INSTALL(TARGETS cpp_helpers
#        LIBRARY DESTINATION /usr/local/lib
#        )

set_target_properties(cpp_helpers PROPERTIES PUBLIC_HEADER "model_cpp_interface.h;model_cpp.h")
INSTALL(TARGETS cpp_helpers
        PUBLIC_HEADER DESTINATION /usr/local/include/rdbfw
)
        #LIBRARY DESTINATION /usr/local/lib

