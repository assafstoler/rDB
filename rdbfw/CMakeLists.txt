CMAKE_MINIMUM_REQUIRED(VERSION 2.8 FATAL_ERROR)

PROJECT(rdbfw)


#SET(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
#ADD_DEFINITIONS(-DPACKAGE_VERSION="${PACKAGE_VERSION}")
#ADD_DEFINITIONS(-DPACKAGE_STRING="${PACKAGE_STRING}")

#SET (MAIN_DIR ${CMAKE_SOURCE_DIR})
#get_filename_component(MAIN_DIR ${CMAKE_SOURCE_DIR} PATH)
#SET(TEST_DIR ${MAIN_DIR}/t)

#MESSAGE(---> ${MAIN_DIR})

#INCLUDE_DIRECTORIES(${MAIN_DIR})
#INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR}) # for generated files

#Uncomment below to build / link modules statically into a single deliverabe
#set (STATIC_BUILD true)

#Uncomemnt below to build fw itself as a shared library (no main)
#set (SHARED_ONLY true)

if (STATIC_BUILD)
    ADD_DEFINITIONS(-DSTATIC_BUILD)
    MESSAGE("Building a statically linked deliverable")

endif()
if (SHARED_ONLY)
    ADD_DEFINITIONS(-DSHARED_ONLY)
endif()

if (NOT APPLE)
    ADD_DEFINITIONS(-DUSE_PRCTL)
endif()

ADD_DEFINITIONS(-DUSE_MSG_BUFFERS)

#This will count how many messages (events) each module got.
ADD_DEFINITIONS(-DMSG_ACCOUNTING)

ADD_DEFINITIONS(-DWAKEUP_ACCOUNTING)

if (SHARED_ONLY)
    MESSAGE("Building a Shared-Only build (library)")
    if(APPLE)
    	ADD_LIBRARY(rdbfwlib SHARED rdbfw.c messaging.c utils.c timers.c rdbfw.h)
    else()
    	ADD_LIBRARY(rdbfwlib SHARED rdbfw.c messaging.c utils.c timers.c hw_timers.c rdbfw.h)
    endif()
    target_link_libraries (rdbfwlib pthread dl rdb_beta)
else()
    ADD_EXECUTABLE(rdbfw rdbfw.c messaging.c utils.c rdbfw.h hw_timers.c timers.c skeleton.c event_driven_skeleton.c)
    target_link_libraries (rdbfw pthread dl rdb_beta rt)
    
    ADD_EXECUTABLE(rdbfw-test rdbfw.c messaging.c utils.c rdbfw.h hw_timers.c timers.c ../src/rDB.h)
    target_link_libraries (rdbfw-test pthread dl rdb_beta rt)
    set_target_properties(rdbfw-test PROPERTIES COMPILE_DEFINITIONS "TESTER")
endif()

if (NOT STATIC_BUILD)
    ADD_LIBRARY(event_skeleton SHARED event_driven_skeleton.c)
    target_link_libraries (event_skeleton pthread dl rdb_beta)

    ADD_LIBRARY(skeleton SHARED skeleton.c utils.c messaging.c)
    target_link_libraries (skeleton pthread dl rdb_beta rt)

    ADD_LIBRARY(timers SHARED timers.c utils.c messaging.c)
    target_link_libraries (timers pthread dl rdb_beta rt)

    ADD_LIBRARY(hw_timers SHARED hw_timers.c utils.c messaging.c)
    target_link_libraries (hw_timers pthread dl rdb_beta rt)
endif()



if (SHARED_ONLY)
    install(TARGETS rdbfwlib DESTINATION bin)
else ()
    install(TARGETS rdbfw  DESTINATION bin)
endif()

if (NOT STATIC_BUILD)
    install(TARGETS hw_timers timers skeleton event_skeleton DESTINATION bin)
endif()

add_subdirectory(test)

enable_testing()
